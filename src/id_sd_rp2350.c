/*
 * ID_SD.C - Sound Manager for RP2350 (I2S + MAME OPL)
 *
 * Implements OPL music (IMF sequencer), AdLib sound effects, PC speaker
 * square-wave synthesis, and digitized sound playback through I2S DMA
 * with the MAME YM3812 OPL emulator.
 *
 * Architecture:
 *   Game thread  -> SD_PlaySound / SD_StartMusic / SD_PlayDigitized
 *   DMA IRQ      -> wolf_audio_fill -> OPL synthesis + PC speaker + digi mix
 *   I2S hardware -> DMA ping-pong -> PIO -> I2S DAC
 */

#include "wl_def.h"
#include "mame/fmopl.h"
#include "audio.h"

#include <string.h>

#define ORIGSAMPLERATE 7042

/*==========================================================================
 * Globals (accessed by game code)
 *==========================================================================*/

boolean         AdLibPresent,
                SoundBlasterPresent, SBProPresent,
                SoundPositioned;
byte            SoundMode;
byte            MusicMode;
byte            DigiMode;
static byte   **SoundTable;
int             DigiMap[LASTSOUND];
int             DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

globalsoundpos channelSoundPos[MIX_CHANNELS];

/*==========================================================================
 * Internal state
 *==========================================================================*/

static boolean  SD_Started;
static boolean  nextsoundpos;
static int      SoundNumber;
static int      DigiNumber;
static word     SoundPriority;
static word     DigiPriority;
static int      LeftPosition;
static int      RightPosition;

word            NumDigi;
digiinfo       *DigiList;
static boolean  DigiPlaying;

/* PC Sound variables */
static volatile byte    pcLastSample;
static byte * volatile  pcSound;
static longword         pcLengthLeft;

/* AdLib variables */
static byte * volatile  alSound;
static byte             alBlock;
static longword         alLengthLeft;
static longword         alTimeCount;
static Instrument       alZeroInst;

/* Sequencer variables */
static volatile boolean sqActive;
static word            *sqHack;
static word            *sqHackPtr;
static int              sqHackLen;
static int              sqHackSeqLen;
static longword         sqHackTime;

static const int oplChip = 0;

int samplesPerMusicTick;

/*==========================================================================
 * IMF music player state (ported from SDL_IMFMusicPlayer)
 *==========================================================================*/

static int numreadysamples = 0;
static byte *curAlSound = 0;
static byte *curAlSoundPtr = 0;
static longword curAlLengthLeft = 0;
static int soundTimeCounter = 5;

/*==========================================================================
 * PC speaker synthesis state (ported from SDL_PCMixCallback)
 *==========================================================================*/

#define SQUARE_WAVE_AMP 0x2000

static int pc_remaining = 0;
static int pc_freq = 0;
static int pc_phase = 0;

/*==========================================================================
 * Digitized sound system
 *==========================================================================*/

#define NUM_DIGI_CHANNELS 6

/* Fixed-point 16.16 resampling: step = ORIGSAMPLERATE / param_samplerate */
static uint32_t digi_step;   /* set in SD_Startup */

typedef struct {
    const byte *data;         /* raw 8-bit unsigned PCM at 7042 Hz */
    int      length;          /* number of raw samples */
    uint32_t frac_pos;        /* 16.16 fixed-point position into raw data */
    uint8_t  left_vol;
    uint8_t  right_vol;
    volatile boolean active;
} digi_channel_t;

static digi_channel_t digi_channels[NUM_DIGI_CHANNELS];

/* Pointers to raw digi sound data (inside VSWAP pages, zero-copy) */
typedef struct {
    const byte *data;
    int         length;
} sound_chunk_t;

static sound_chunk_t SoundChunks[STARTMUSIC - STARTDIGISOUNDS];

/*==========================================================================
 * OPL synthesis temp buffer (SRAM for ISR performance)
 *==========================================================================*/

#define OPL_TEMP_BUF_SIZE 1536
static int32_t opl_temp_buf[OPL_TEMP_BUF_SIZE];

/*==========================================================================
 * Forward declarations
 *==========================================================================*/

static void wolf_audio_fill(int buf_index, uint32_t *buf, uint32_t frames);

/*==========================================================================
 * Utility
 *==========================================================================*/

void Delay(int32_t wolfticks) {
    if (wolfticks > 0)
        SDL_Delay((wolfticks * 100) / 7);
}

static void SDL_SoundFinished(void) {
    SoundNumber = 0;
    SoundPriority = 0;
}

/*==========================================================================
 * Digitized sound support
 *==========================================================================*/

void SD_StopDigitized(void) {
    DigiPlaying = false;
    DigiNumber = 0;
    DigiPriority = 0;
    SoundPositioned = false;

    if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        SDL_SoundFinished();

    switch (DigiMode) {
        case sds_PC:
            pcSound = 0;
            break;
        case sds_SoundBlaster:
            for (int i = 0; i < NUM_DIGI_CHANNELS; i++)
                digi_channels[i].active = false;
            break;
        default:
            break;
    }
}

int SD_GetChannelForDigi(int which) {
    /* If this sound has a dedicated channel, use it */
    if (DigiChannel[which] != -1) {
        int ch = DigiChannel[which];
        if (ch >= 0 && ch < NUM_DIGI_CHANNELS)
            return ch;
    }

    /* Find a free channel (prefer 2+ to keep 0-1 for reserved sounds) */
    for (int i = 2; i < NUM_DIGI_CHANNELS; i++) {
        if (!digi_channels[i].active)
            return i;
    }
    /* Check reserved channels too */
    for (int i = 0; i < 2; i++) {
        if (!digi_channels[i].active)
            return i;
    }

    /* All busy — steal the channel closest to finishing */
    int best = 2;
    int best_progress = 0;
    for (int i = 2; i < NUM_DIGI_CHANNELS; i++) {
        int idx = (int)(digi_channels[i].frac_pos >> 16);
        int progress = digi_channels[i].length > 0
            ? (idx * 100) / digi_channels[i].length
            : 100;
        if (progress > best_progress) {
            best_progress = progress;
            best = i;
        }
    }
    return best;
}

void SD_SetPosition(int channel, int leftpos, int rightpos) {
    if ((leftpos < 0) || (leftpos > 15) || (rightpos < 0) || (rightpos > 15)
            || ((leftpos == 15) && (rightpos == 15)))
        Quit("SD_SetPosition: Illegal position");

    if (channel >= 0 && channel < NUM_DIGI_CHANNELS) {
        digi_channels[channel].left_vol  = (uint8_t)(255 - leftpos * 17);
        digi_channels[channel].right_vol = (uint8_t)(255 - rightpos * 17);
    }
}

void SD_PrepareSound(int which) {
    if (DigiList == NULL)
        Quit("SD_PrepareSound(%i): DigiList not initialized!\n", which);

    if (which < 0 || which >= STARTMUSIC - STARTDIGISOUNDS)
        return;

    int page = DigiList[which].startpage;
    int size = DigiList[which].length;

    byte *origsamples = PM_GetSoundPage(page);
    if (!origsamples || origsamples + size >= PM_GetPageEnd())
        return;

    /* Zero-copy: just point to the raw 8-bit data in VSWAP pages.
     * Resampling from 7042 Hz to 44100 Hz happens in the mixer. */
    SoundChunks[which].data   = origsamples;
    SoundChunks[which].length = size;
}

int SD_PlayDigitized(word which, int leftpos, int rightpos) {
    if (!DigiMode)
        return 0;

    if (which >= NumDigi)
        Quit("SD_PlayDigitized: bad sound number %i", which);

    int channel = SD_GetChannelForDigi(which);
    SD_SetPosition(channel, leftpos, rightpos);

    DigiPlaying = true;

    sound_chunk_t *chunk = &SoundChunks[which];
    if (!chunk->data || !chunk->length) {
        return 0;
    }

    digi_channels[channel].data     = chunk->data;
    digi_channels[channel].length   = chunk->length;
    digi_channels[channel].frac_pos = 0;
    digi_channels[channel].active   = true;

    return channel;
}

void SD_ChannelFinished(int channel) {
    channelSoundPos[channel].valid = 0;
}

void SD_SetDigiDevice(byte mode) {
    boolean devicenotpresent = false;

    if (mode == DigiMode)
        return;

    SD_StopDigitized();

    switch (mode) {
        case sds_SoundBlaster:
            if (!SoundBlasterPresent)
                devicenotpresent = true;
            break;
        default:
            break;
    }

    if (!devicenotpresent)
        DigiMode = mode;
}

void SDL_SetupDigi(void) {
    word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);
    NumDigi = (word)PM_GetPageSize(ChunksInFile - 1) / 4;

    DigiList = (digiinfo *)malloc(NumDigi * sizeof(*DigiList));
    int i, page;
    for (i = 0; i < NumDigi; i++) {
        DigiList[i].startpage = soundInfoPage[i * 2];
        if ((int)DigiList[i].startpage >= ChunksInFile - 1) {
            NumDigi = i;
            break;
        }

        int lastPage;
        if (i < NumDigi - 1) {
            lastPage = soundInfoPage[i * 2 + 2];
            if (lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1)
                lastPage = ChunksInFile - 1;
            else
                lastPage += PMSoundStart;
        } else {
            lastPage = ChunksInFile - 1;
        }

        int size = 0;
        for (page = PMSoundStart + DigiList[i].startpage; page < lastPage; page++)
            size += PM_GetPageSize(page);

        if (lastPage == ChunksInFile - 1 && PMSoundInfoPagePadded)
            size--;

        if ((size & 0xffff0000) != 0 && (size & 0xffff) < soundInfoPage[i * 2 + 1])
            size -= 0x10000;
        size = (size & 0xffff0000) | soundInfoPage[i * 2 + 1];

        DigiList[i].length = size;
    }

    for (i = 0; i < LASTSOUND; i++) {
        DigiMap[i] = -1;
        DigiChannel[i] = -1;
    }
}

/*==========================================================================
 * AdLib / OPL
 *==========================================================================*/

static void SDL_ALStopSound(void) {
    alSound = 0;
    alOut(alFreqH + 0, 0);
}

static void SDL_AlSetFXInst(Instrument *inst) {
    byte c, m;
    m = 0;
    c = 3;
    alOut(m + alChar, inst->mChar);
    alOut(m + alScale, inst->mScale);
    alOut(m + alAttack, inst->mAttack);
    alOut(m + alSus, inst->mSus);
    alOut(m + alWave, inst->mWave);
    alOut(c + alChar, inst->cChar);
    alOut(c + alScale, inst->cScale);
    alOut(c + alAttack, inst->cAttack);
    alOut(c + alSus, inst->cSus);
    alOut(c + alWave, inst->cWave);
    alOut(alFeedCon, 0);
}

static void SDL_ALPlaySound(AdLibSound *sound) {
    Instrument *inst;
    byte *data;

    SDL_ALStopSound();

    alLengthLeft = sound->common.length;
    data = sound->data;
    alBlock = ((sound->block & 7) << 2) | 0x20;
    inst = &sound->inst;

    if (!(inst->mSus | inst->cSus))
        Quit("SDL_ALPlaySound() - Bad instrument");

    SDL_AlSetFXInst(inst);
    alSound = (byte *)data;
}

static void SDL_ShutAL(void) {
    alSound = 0;
    alOut(alEffects, 0);
    alOut(alFreqH + 0, 0);
    SDL_AlSetFXInst(&alZeroInst);
}

static void SDL_StartAL(void) {
    alOut(alEffects, 0);
    SDL_AlSetFXInst(&alZeroInst);
}

static void SDL_PCPlaySound(PCSound *sound) {
    pcLastSample = (byte)-1;
    pcLengthLeft = sound->common.length;
    pcSound = sound->data;
}

static void SDL_PCStopSound(void) {
    pcSound = 0;
}

static void SDL_ShutPC(void) {
    pcSound = 0;
}

static void SDL_ShutDevice(void) {
    switch (SoundMode) {
        case sdm_PC: SDL_ShutPC(); break;
        case sdm_AdLib: SDL_ShutAL(); break;
        default: break;
    }
    SoundMode = sdm_Off;
}

static void SDL_StartDevice(void) {
    switch (SoundMode) {
        case sdm_AdLib: SDL_StartAL(); break;
        default: break;
    }
    SoundNumber = 0;
    SoundPriority = 0;
}

/*==========================================================================
 * Audio fill callback — called from DMA IRQ to generate stereo audio.
 *
 * This is the heart of the audio system, replacing SDL_IMFMusicPlayer
 * and SDL_PCMixCallback.  It runs at DMA completion rate (~23 ms for
 * 1024 frames at 44100 Hz).
 *
 * Flow per buffer:
 *   1. Generate mono OPL samples (music + AdLib SFX via YM3812)
 *   2. Convert to stereo, mixing in PC speaker square wave
 *   3. Mix in active digitized sound channels with L/R panning
 *   4. Advance the IMF sequencer and AdLib SFX state each tick
 *==========================================================================*/

static void wolf_audio_fill(int buf_index, uint32_t *buf, uint32_t frames) {
    (void)buf_index;
    int16_t *out = (int16_t *)buf;
    uint32_t remaining = frames;

    while (remaining > 0) {
        if (numreadysamples > 0) {
            /* Generate a chunk of OPL + mix everything */
            uint32_t n = (uint32_t)numreadysamples;
            if (n > remaining) n = remaining;
            if (n > OPL_TEMP_BUF_SIZE) n = OPL_TEMP_BUF_SIZE;

            /* OPL synthesis (mono) */
            YM3812UpdateOne(oplChip, (INT32 *)opl_temp_buf, (int)n);

            /* Per-sample: convert 32-bit mono OPL to stereo, mix sources.
             * OPL raw accumulator peaks at ~74K for 9 FM channels.
             * >>1 gives good volume; brief peaks may clip at output
             * but this is inaudible vs the old internal hard-clipping. */
            for (uint32_t i = 0; i < n; i++) {
                int32_t opl = opl_temp_buf[i];
                int32_t left  = opl;
                int32_t right = opl;

                /* ---- PC speaker square wave synthesis ---- */
                while (pc_remaining == 0) {
                    pc_phase = 0;
                    if (pcSound) {
                        /* PC speaker sample rate is 140 Hz */
                        pc_remaining = param_samplerate / 140;
                        if (*pcSound != pcLastSample) {
                            pcLastSample = *pcSound;
                            if (pcLastSample)
                                /* PIC counts at 1.193180 MHz, reload = sample*60 */
                                pc_freq = 1193180 / (pcLastSample * 60);
                            else
                                pc_freq = 0;
                        }
                        pcSound++;
                        pcLengthLeft--;
                        if (!pcLengthLeft) {
                            pcSound = 0;
                            SoundNumber = 0;
                            SoundPriority = 0;
                        }
                    } else {
                        pc_freq = 0;
                        pc_remaining = 1;
                    }
                }

                if (pc_freq != 0) {
                    int frac = (pc_phase * pc_freq * 2) / param_samplerate;
                    int16_t pc_val = (frac % 2 == 0)
                        ? SQUARE_WAVE_AMP : (int16_t)-SQUARE_WAVE_AMP;
                    left  += pc_val >> 1;
                    right += pc_val >> 1;
                    pc_phase++;
                }
                pc_remaining--;

                /* ---- Digitized sound mixing (real-time 7042→44100 resample) ---- */
                for (int ch = 0; ch < NUM_DIGI_CHANNELS; ch++) {
                    if (digi_channels[ch].active) {
                        uint32_t fp = digi_channels[ch].frac_pos;
                        int idx = (int)(fp >> 16);
                        if (idx < digi_channels[ch].length) {
                            /* Linear interpolation between adjacent 8-bit samples */
                            int s0 = (int)digi_channels[ch].data[idx] - 128;
                            int s1 = (idx + 1 < digi_channels[ch].length)
                                   ? (int)digi_channels[ch].data[idx + 1] - 128 : s0;
                            int frac = (fp >> 8) & 0xFF; /* 8-bit fraction */
                            int32_t s = (s0 * (256 - frac) + s1 * frac); /* 16-bit result */
                            left  += (s * digi_channels[ch].left_vol) >> 10;
                            right += (s * digi_channels[ch].right_vol) >> 10;
                            digi_channels[ch].frac_pos = fp + digi_step;
                        } else {
                            digi_channels[ch].active = false;
                        }
                    }
                }

                /* Clamp to int16 range */
                if (left > 32767) left = 32767;
                else if (left < -32768) left = -32768;
                if (right > 32767) right = 32767;
                else if (right < -32768) right = -32768;

                out[i * 2]     = (int16_t)left;
                out[i * 2 + 1] = (int16_t)right;
            }

            out += n * 2;
            remaining -= n;
            numreadysamples -= (int)n;

        } else {
            /* ---- Advance sound/music state (one tick at 700 Hz) ---- */

            /* AdLib SFX advance every 5 ticks (= 140 Hz) */
            soundTimeCounter--;
            if (!soundTimeCounter) {
                soundTimeCounter = 5;
                if (curAlSound != alSound) {
                    curAlSound = curAlSoundPtr = alSound;
                    curAlLengthLeft = alLengthLeft;
                }
                if (curAlSound) {
                    if (*curAlSoundPtr) {
                        alOut(alFreqL, *curAlSoundPtr);
                        alOut(alFreqH, alBlock);
                    } else {
                        alOut(alFreqH, 0);
                    }
                    curAlSoundPtr++;
                    curAlLengthLeft--;
                    if (!curAlLengthLeft) {
                        curAlSound = alSound = 0;
                        SoundNumber = 0;
                        SoundPriority = 0;
                        alOut(alFreqH, 0);
                    }
                }
            }

            /* IMF music sequencer */
            if (sqActive) {
                do {
                    if (sqHackTime > alTimeCount) break;
                    sqHackTime = alTimeCount + *(sqHackPtr + 1);
                    alOut(*(byte *)sqHackPtr, *(((byte *)sqHackPtr) + 1));
                    sqHackPtr += 2;
                    sqHackLen -= 4;
                } while (sqHackLen > 0);
                alTimeCount++;
                if (!sqHackLen) {
                    sqHackPtr = sqHack;
                    sqHackLen = sqHackSeqLen;
                    sqHackTime = 0;
                    alTimeCount = 0;
                }
            }

            numreadysamples = samplesPerMusicTick;
        }
    }
}

/*==========================================================================
 * Public routines
 *==========================================================================*/

boolean SD_SetSoundMode(byte mode) {
    boolean result = false;
    word tableoffset;

    SD_StopSound();

    if ((mode == sdm_AdLib) && !AdLibPresent)
        mode = sdm_PC;

    switch (mode) {
        case sdm_Off:
            tableoffset = STARTADLIBSOUNDS;
            result = true;
            break;
        case sdm_PC:
            tableoffset = STARTPCSOUNDS;
            result = true;
            break;
        case sdm_AdLib:
            tableoffset = STARTADLIBSOUNDS;
            if (AdLibPresent) result = true;
            break;
        default:
            Quit("SD_SetSoundMode: Invalid sound mode %i", mode);
            return false;
    }
    SoundTable = &audiosegs[tableoffset];

    if (result && (mode != SoundMode)) {
        SDL_ShutDevice();
        SoundMode = mode;
        SDL_StartDevice();
    }
    return result;
}

boolean SD_SetMusicMode(byte mode) {
    boolean result = false;

    SD_FadeOutMusic();
    while (SD_MusicPlaying())
        SDL_Delay(5);

    switch (mode) {
        case smm_Off: result = true; break;
        case smm_AdLib:
            if (AdLibPresent) result = true;
            break;
    }

    if (result) MusicMode = mode;
    return result;
}

void SD_Startup(void) {
    int i;

    if (SD_Started) return;

    samplesPerMusicTick = param_samplerate / 700;

    /* Initialize OPL emulator */
    if (YM3812Init(1, 3579545, param_samplerate)) {
        printf("Unable to create virtual OPL!!\n");
    }

    for (i = 1; i < 0xf6; i++)
        YM3812Write(oplChip, i, 0);

    YM3812Write(oplChip, 1, 0x20);  /* Set WSE=1 */

    AdLibPresent = true;
    SoundBlasterPresent = true;

    alTimeCount = 0;

    /* Fixed-point resample step: ORIGSAMPLERATE / param_samplerate in 16.16 */
    digi_step = ((uint32_t)ORIGSAMPLERATE << 16) / (uint32_t)param_samplerate;

    /* Clear audio state */
    memset(digi_channels, 0, sizeof(digi_channels));
    memset(SoundChunks, 0, sizeof(SoundChunks));

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    SDL_SetupDigi();

    /* Register audio callback and start I2S playback */
    i2s_set_fill_callback(wolf_audio_fill);
    i2s_start();

    SD_Started = true;

    printf("Sound system started (OPL + I2S @ %d Hz, %d samples/tick)\n",
           param_samplerate, samplesPerMusicTick);
}

void SD_Shutdown(void) {
    if (!SD_Started) return;

    SD_MusicOff();
    SD_StopSound();

    /* Stop I2S DMA before any cleanup */
    i2s_set_fill_callback(NULL);

    free(DigiList);
    DigiList = NULL;

    SD_Started = false;
}

void SD_PositionSound(int leftvol, int rightvol) {
    LeftPosition = leftvol;
    RightPosition = rightvol;
    nextsoundpos = true;
}

boolean SD_PlaySound(int sound) {
    boolean ispos;
    SoundCommon *s;
    int lp, rp;

    lp = LeftPosition;
    rp = RightPosition;
    LeftPosition = 0;
    RightPosition = 0;

    ispos = nextsoundpos;
    nextsoundpos = false;

    if (sound == -1 || (DigiMode == sds_Off && SoundMode == sdm_Off))
        return 0;

    s = (SoundCommon *)SoundTable[sound];

    if ((SoundMode != sdm_Off) && !s)
        Quit("SD_PlaySound() - Uncached sound");

    if ((DigiMode != sds_Off) && (DigiMap[sound] != -1)) {
        if ((DigiMode == sds_PC) && (SoundMode == sdm_PC)) {
            if (s->priority < SoundPriority) return 0;
            SDL_PCStopSound();
            SD_PlayDigitized(DigiMap[sound], lp, rp);
            SoundPositioned = ispos;
            SoundNumber = sound;
            SoundPriority = s->priority;
        } else {
            int channel = SD_PlayDigitized(DigiMap[sound], lp, rp);
            SoundPositioned = ispos;
            DigiNumber = sound;
            DigiPriority = s->priority;
            return channel + 1;
        }
        return true;
    }

    if (SoundMode == sdm_Off) return 0;

    if (!s->length)
        Quit("SD_PlaySound() - Zero length sound");
    if (s->priority < SoundPriority) return 0;

    switch (SoundMode) {
        case sdm_PC:
            SDL_PCPlaySound((PCSound *)s);
            break;
        case sdm_AdLib:
            curAlSound = alSound = 0;
            alOut(alFreqH, 0);
            SDL_ALPlaySound((AdLibSound *)s);
            break;
        default: break;
    }

    SoundNumber = sound;
    SoundPriority = s->priority;
    return 0;
}

word SD_SoundPlaying(void) {
    boolean result = false;

    switch (SoundMode) {
        case sdm_PC:
            result = pcSound ? true : false;
            break;
        case sdm_AdLib:
            result = alSound ? true : false;
            break;
        default:
            break;
    }

    if (result)
        return SoundNumber;
    else
        return false;
}

void SD_StopSound(void) {
    if (DigiPlaying)
        SD_StopDigitized();

    switch (SoundMode) {
        case sdm_PC:
            SDL_PCStopSound();
            break;
        case sdm_AdLib:
            SDL_ALStopSound();
            break;
        default:
            break;
    }

    SoundPositioned = false;
    SDL_SoundFinished();
}

void SD_WaitSoundDone(void) {
    while (SD_SoundPlaying())
        SDL_Delay(5);
}

void SD_MusicOn(void) {
    sqActive = true;
}

int SD_MusicOff(void) {
    word i;
    sqActive = false;
    switch (MusicMode) {
        case smm_AdLib:
            alOut(alEffects, 0);
            for (i = 0; i < sqMaxTracks; i++)
                alOut(alFreqH + i + 1, 0);
            break;
        default: break;
    }
    return (int)(sqHackPtr - sqHack);
}

void SD_StartMusic(int chunk) {
    SD_MusicOff();

    if (MusicMode == smm_AdLib) {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        sqHack = (word *)(void *)audiosegs[chunk];
        if (*sqHack == 0)
            sqHackLen = sqHackSeqLen = chunkLen;
        else
            sqHackLen = sqHackSeqLen = *sqHack++;
        sqHackPtr = sqHack;
        sqHackTime = 0;
        alTimeCount = 0;
        SD_MusicOn();
    }
}

void SD_ContinueMusic(int chunk, int startoffs) {
    int i;

    SD_MusicOff();

    if (MusicMode == smm_AdLib) {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);
        sqHack = (word *)(void *)audiosegs[chunk];
        if (*sqHack == 0)
            sqHackLen = sqHackSeqLen = chunkLen;
        else
            sqHackLen = sqHackSeqLen = *sqHack++;
        sqHackPtr = sqHack;

        if (startoffs >= sqHackLen)
            startoffs = 0;

        for (i = 0; i < startoffs; i += 2) {
            byte reg = *(byte *)sqHackPtr;
            byte val = *(((byte *)sqHackPtr) + 1);
            if (reg >= 0xb1 && reg <= 0xb8) val &= 0xdf;
            else if (reg == 0xbd) val &= 0xe0;
            alOut(reg, val);
            sqHackPtr += 2;
            sqHackLen -= 4;
        }
        sqHackTime = 0;
        alTimeCount = 0;
        SD_MusicOn();
    }
}

void SD_FadeOutMusic(void) {
    switch (MusicMode) {
        case smm_AdLib:
            SD_MusicOff();
            break;
        default: break;
    }
}

boolean SD_MusicPlaying(void) {
    boolean result;
    switch (MusicMode) {
        case smm_AdLib: result = sqActive; break;
        default: result = false; break;
    }
    return result;
}
