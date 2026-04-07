/*
 * ID_SD.C - Sound Manager for RP2350 (I2S + MAME OPL)
 *
 * Replaces SDL_mixer-based audio with I2S output and MAME OPL emulation.
 * Currently a stub implementation - audio will be added incrementally.
 */

#include "wl_def.h"
#include "mame/fmopl.h"

#define ORIGSAMPLERATE 7042

// Global variables
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

// Internal variables
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

// PC Sound variables
static volatile byte    pcLastSample;
static byte * volatile  pcSound;
static longword         pcLengthLeft;

// AdLib variables
static byte * volatile  alSound;
static byte             alBlock;
static longword         alLengthLeft;
static longword         alTimeCount;
static Instrument       alZeroInst;

// Sequencer variables
static volatile boolean sqActive;
static word            *sqHack;
static word            *sqHackPtr;
static int              sqHackLen;
static int              sqHackSeqLen;
static longword         sqHackTime;

static const int oplChip = 0;

int samplesPerMusicTick;

void Delay(int32_t wolfticks) {
    if (wolfticks > 0)
        SDL_Delay((wolfticks * 100) / 7);
}

static void SDL_SoundFinished(void) {
    SoundNumber = 0;
    SoundPriority = 0;
}

//===========================================================================
// Digitized sound stubs
//===========================================================================

void SD_StopDigitized(void) {
    DigiPlaying = false;
    DigiNumber = 0;
    DigiPriority = 0;
    SoundPositioned = false;
    if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        SDL_SoundFinished();
}

int SD_GetChannelForDigi(int which) {
    (void)which;
    return 0;
}

void SD_SetPosition(int channel, int leftpos, int rightpos) {
    (void)channel; (void)leftpos; (void)rightpos;
}

Sint16 GetSample(float csample, byte *samples, int size) {
    float s0 = 0, s1 = 0, s2 = 0;
    int cursample = (int)csample;
    float sf = csample - (float)cursample;

    if (cursample - 1 >= 0) s0 = (float)(samples[cursample - 1] - 128);
    s1 = (float)(samples[cursample] - 128);
    if (cursample + 1 < size) s2 = (float)(samples[cursample + 1] - 128);

    float val = s0 * sf * (sf - 1) / 2 - s1 * (sf * sf - 1) + s2 * (sf + 1) * sf / 2;
    int32_t intval = (int32_t)(val * 256);
    if (intval < -32768) intval = -32768;
    else if (intval > 32767) intval = 32767;
    return (Sint16)intval;
}

void SD_PrepareSound(int which) {
    // Stub - no SDL_mixer on RP2350
    (void)which;
}

int SD_PlayDigitized(word which, int leftpos, int rightpos) {
    (void)which; (void)leftpos; (void)rightpos;
    // Stub - no digitized sound playback yet
    return 0;
}

void SD_ChannelFinished(int channel) {
    channelSoundPos[channel].valid = 0;
}

void SD_SetDigiDevice(byte mode) {
    if (mode == DigiMode) return;
    SD_StopDigitized();
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

//===========================================================================
// AdLib / OPL stubs
//===========================================================================

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

//===========================================================================
// Public routines
//===========================================================================

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

    // Initialize OPL emulator
    if (YM3812Init(1, 3579545, param_samplerate)) {
        printf("Unable to create virtual OPL!!\n");
    }

    for (i = 1; i < 0xf6; i++)
        YM3812Write(oplChip, i, 0);

    YM3812Write(oplChip, 1, 0x20);

    AdLibPresent = true;
    SoundBlasterPresent = true;

    alTimeCount = 0;

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    SDL_SetupDigi();

    SD_Started = true;
}

void SD_Shutdown(void) {
    if (!SD_Started) return;

    SD_MusicOff();
    SD_StopSound();

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
            alSound = 0;
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
    // No audio callback running on RP2350 yet, so sounds never
    // finish on their own.  Report nothing playing to avoid hangs.
    return 0;
}

void SD_StopSound(void) {
    if (DigiPlaying) SD_StopDigitized();

    // Force-clear both channels since we have no playback callback
    pcSound = 0;
    alSound = 0;

    SoundPositioned = false;
    SDL_SoundFinished();
}

void SD_WaitSoundDone(void) {
    // No-op: no audio playback callback, nothing to wait for
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
