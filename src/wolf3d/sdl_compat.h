/*
 * SDL Compatibility Layer for RP2350 (bare metal)
 *
 * Provides SDL2-compatible types, constants, and function stubs
 * so that Wolf4SDL source files compile without modification.
 */

#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*==========================================================================
 * SDL Basic Types
 *==========================================================================*/

typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint8_t  Bit8u;
typedef uint32_t Bit32u;
typedef int32_t  Bit32s;

#define SDL_ALPHA_OPAQUE 255

/*==========================================================================
 * SDL_Color
 *==========================================================================*/

typedef struct {
    uint8_t r, g, b, a;
} SDL_Color;

/*==========================================================================
 * SDL_Surface (simplified for 8-bit indexed)
 *==========================================================================*/

typedef struct {
    SDL_Color colors[256];
} SDL_Palette;

typedef struct {
    int BytesPerPixel;
    SDL_Palette *palette;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    int w, h;
    int pitch;
    void *pixels;
    SDL_PixelFormat *format;
} SDL_Surface;

typedef void SDL_Window;
typedef void SDL_Renderer;
typedef void SDL_Texture;

/*==========================================================================
 * SDL_Event types (simplified)
 *==========================================================================*/

typedef struct {
    int type;
    struct {
        struct {
            int scancode;
        } keysym;
    } key;
    struct {
        char text[32];
    } text;
    struct {
        int button;
    } jbutton;
} SDL_Event;

#define SDL_QUIT            0x100
#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_TEXTINPUT       0x303
#define SDL_JOYBUTTONDOWN   0x600
#define SDL_JOYBUTTONUP     0x601
#define SDL_MOUSEMOTION     0x400

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

/*==========================================================================
 * SDL_RWops stub
 *==========================================================================*/

typedef struct {
    void *mem;
    int   size;
    int   pos;
} SDL_RWops;

static inline SDL_RWops* SDL_RWFromMem(void *mem, int size) {
    (void)mem; (void)size;
    return NULL;
}

/*==========================================================================
 * SDL Scancode defines (matching SDL2 values)
 *==========================================================================*/

#define SDL_SCANCODE_UNKNOWN    0
#define SDL_SCANCODE_A          4
#define SDL_SCANCODE_B          5
#define SDL_SCANCODE_C          6
#define SDL_SCANCODE_D          7
#define SDL_SCANCODE_E          8
#define SDL_SCANCODE_F          9
#define SDL_SCANCODE_G          10
#define SDL_SCANCODE_H          11
#define SDL_SCANCODE_I          12
#define SDL_SCANCODE_J          13
#define SDL_SCANCODE_K          14
#define SDL_SCANCODE_L          15
#define SDL_SCANCODE_M          16
#define SDL_SCANCODE_N          17
#define SDL_SCANCODE_O          18
#define SDL_SCANCODE_P          19
#define SDL_SCANCODE_Q          20
#define SDL_SCANCODE_R          21
#define SDL_SCANCODE_S          22
#define SDL_SCANCODE_T          23
#define SDL_SCANCODE_U          24
#define SDL_SCANCODE_V          25
#define SDL_SCANCODE_W          26
#define SDL_SCANCODE_X          27
#define SDL_SCANCODE_Y          28
#define SDL_SCANCODE_Z          29
#define SDL_SCANCODE_1          30
#define SDL_SCANCODE_2          31
#define SDL_SCANCODE_3          32
#define SDL_SCANCODE_4          33
#define SDL_SCANCODE_5          34
#define SDL_SCANCODE_6          35
#define SDL_SCANCODE_7          36
#define SDL_SCANCODE_8          37
#define SDL_SCANCODE_9          38
#define SDL_SCANCODE_0          39
#define SDL_SCANCODE_RETURN     40
#define SDL_SCANCODE_ESCAPE     41
#define SDL_SCANCODE_BACKSPACE  42
#define SDL_SCANCODE_TAB        43
#define SDL_SCANCODE_SPACE      44
#define SDL_SCANCODE_MINUS      45
#define SDL_SCANCODE_EQUALS     46
#define SDL_SCANCODE_CAPSLOCK   57
#define SDL_SCANCODE_F1         58
#define SDL_SCANCODE_F2         59
#define SDL_SCANCODE_F3         60
#define SDL_SCANCODE_F4         61
#define SDL_SCANCODE_F5         62
#define SDL_SCANCODE_F6         63
#define SDL_SCANCODE_F7         64
#define SDL_SCANCODE_F8         65
#define SDL_SCANCODE_F9         66
#define SDL_SCANCODE_F10        67
#define SDL_SCANCODE_F11        68
#define SDL_SCANCODE_F12        69
#define SDL_SCANCODE_SCROLLLOCK 71
#define SDL_SCANCODE_PAUSE      72
#define SDL_SCANCODE_INSERT     73
#define SDL_SCANCODE_HOME       74
#define SDL_SCANCODE_PAGEUP     75
#define SDL_SCANCODE_DELETE     76
#define SDL_SCANCODE_END        77
#define SDL_SCANCODE_PAGEDOWN   78
#define SDL_SCANCODE_RIGHT      79
#define SDL_SCANCODE_LEFT       80
#define SDL_SCANCODE_DOWN       81
#define SDL_SCANCODE_UP         82
#define SDL_SCANCODE_KP_2       90
#define SDL_SCANCODE_KP_4       92
#define SDL_SCANCODE_KP_5       93
#define SDL_SCANCODE_KP_6       94
#define SDL_SCANCODE_KP_8       96
#define SDL_SCANCODE_KP_ENTER   88
#define SDL_SCANCODE_LCTRL      224
#define SDL_SCANCODE_LSHIFT     225
#define SDL_SCANCODE_LALT       226
#define SDL_SCANCODE_RCTRL      228
#define SDL_SCANCODE_RSHIFT     229
#define SDL_SCANCODE_RALT       230

#define SDL_NUM_SCANCODES       512

/*==========================================================================
 * SDL Keymod
 *==========================================================================*/

#define KMOD_NUM 0x1000

static inline int SDL_GetModState(void) { return 0; }

/*==========================================================================
 * SDL Init / Quit stubs
 *==========================================================================*/

#define SDL_INIT_VIDEO      0x00000020
#define SDL_INIT_AUDIO      0x00000010
#define SDL_INIT_JOYSTICK   0x00000200

static inline int SDL_Init(uint32_t flags) { (void)flags; return 0; }
static inline void SDL_Quit(void) {}

/*==========================================================================
 * SDL Window / Display stubs
 *==========================================================================*/

#define SDL_WINDOWPOS_UNDEFINED 0
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001
#define SDL_WINDOW_OPENGL 0x0002

#define SDL_PIXELFORMAT_ARGB8888 0x16362004

#define SDL_RENDERER_ACCELERATED 0x02
#define SDL_RENDERER_PRESENTVSYNC 0x04
#define SDL_BLENDMODE_BLEND 1

static inline void SDL_SetHint(const char *name, const char *value) { (void)name; (void)value; }
static inline void SDL_ShowCursor(int toggle) { (void)toggle; }

static inline int SDL_SetRelativeMouseMode(int enabled) { (void)enabled; return 0; }
static inline void SDL_SetWindowGrab(void *window, int grabbed) { (void)window; (void)grabbed; }
static inline void SDL_WarpMouseInWindow(void *window, int x, int y) { (void)window; (void)x; (void)y; }

static inline void SDL_EventState(int type, int state) { (void)type; (void)state; }
#define SDL_IGNORE 0

/*==========================================================================
 * SDL Surface operations
 *==========================================================================*/

#define SDL_MUSTLOCK(s) 0

static inline int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }

static inline int SDL_FillRect(SDL_Surface *dst, void *rect, uint32_t color) {
    if (dst && dst->pixels) {
        memset(dst->pixels, (int)color, dst->pitch * dst->h);
    }
    return 0;
}

static inline void SDL_BlitSurface(SDL_Surface *src, void *srcrect,
                                    SDL_Surface *dst, void *dstrect) {
    (void)srcrect; (void)dstrect;
    if (src && dst && src->pixels && dst->pixels) {
        int minH = src->h < dst->h ? src->h : dst->h;
        for (int y = 0; y < minH; y++) {
            int minPitch = src->pitch < dst->pitch ? src->pitch : dst->pitch;
            memcpy((uint8_t*)dst->pixels + y * dst->pitch,
                   (uint8_t*)src->pixels + y * src->pitch, minPitch);
        }
    }
}

static inline void SDL_FreeSurface(SDL_Surface *s) {
    if (s) {
        free(s->pixels);
        free(s->format);
        free(s);
    }
}

static inline void SDL_SetPaletteColors(SDL_Palette *palette, SDL_Color *colors,
                                         int firstcolor, int ncolors) {
    if (palette && colors) {
        memcpy(&palette->colors[firstcolor], colors, ncolors * sizeof(SDL_Color));
    }
}

static inline void SDL_PixelFormatEnumToMasks(uint32_t format, int *bpp,
    uint32_t *Rmask, uint32_t *Gmask, uint32_t *Bmask, uint32_t *Amask) {
    (void)format;
    *bpp = 32;
    *Amask = 0xFF000000;
    *Rmask = 0x00FF0000;
    *Gmask = 0x0000FF00;
    *Bmask = 0x000000FF;
}

static inline uint32_t SDL_MapRGBA(SDL_PixelFormat *fmt,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    (void)fmt;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/*==========================================================================
 * SDL Joystick stubs
 *==========================================================================*/

typedef void SDL_Joystick;

static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick *SDL_JoystickOpen(int index) { (void)index; return NULL; }
static inline void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
static inline int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
static inline void SDL_JoystickUpdate(void) {}
static inline int SDL_JoystickGetAxis(SDL_Joystick *j, int axis) { (void)j; (void)axis; return 0; }
static inline uint8_t SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return 0; }
static inline int SDL_JoystickGetButton(SDL_Joystick *j, int button) { (void)j; (void)button; return 0; }

#define SDL_HAT_UP    0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN  0x04
#define SDL_HAT_LEFT  0x08

/*==========================================================================
 * SDL Mouse stubs
 *==========================================================================*/

#define SDL_BUTTON(x)          (1 << ((x) - 1))
#define SDL_BUTTON_LEFT        1
#define SDL_BUTTON_MIDDLE      2
#define SDL_BUTTON_RIGHT       3

static inline uint32_t SDL_GetMouseState(int *x, int *y) {
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

uint32_t SDL_GetRelativeMouseState(int *x, int *y);

/*==========================================================================
 * SDL Event stubs
 *==========================================================================*/

int  SDL_PollEvent(SDL_Event *event);
void SDL_WaitEvent_compat(void *unused);
#define SDL_WaitEvent(x) (SDL_WaitEvent_compat(x), 1)

/*==========================================================================
 * SDL Timer
 *==========================================================================*/

uint32_t SDL_GetTicks(void);
void     SDL_Delay(uint32_t ms);

/*==========================================================================
 * SDL_mixer compatibility
 *==========================================================================*/

#define MIX_CHANNELS 8
#define AUDIO_S16    0x8010
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x01

typedef struct {
    uint8_t *abuf;
    uint32_t alen;
} Mix_Chunk;

static inline int Mix_OpenAudioDevice(int freq, uint16_t format, int channels,
    int chunksize, const char *device, int allowed_changes) {
    (void)freq; (void)format; (void)channels;
    (void)chunksize; (void)device; (void)allowed_changes;
    return 0;
}

static inline void Mix_QuerySpec(int *freq, void *format, void *channels) {
    (void)format; (void)channels;
    // keep the frequency as-is
}

static inline int Mix_ReserveChannels(int num) { (void)num; return num; }
static inline int Mix_GroupChannels(int from, int to, int tag) {
    (void)from; (void)to; (void)tag; return 0;
}

static inline void Mix_HookMusic(void (*func)(void*, Uint8*, int), void *arg) {
    (void)func; (void)arg;
}

static inline void Mix_ChannelFinished(void (*func)(int)) { (void)func; }
static inline void Mix_SetPostMix(void (*func)(void*, Uint8*, int), void *arg) {
    (void)func; (void)arg;
}

static inline int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops) {
    (void)channel; (void)chunk; (void)loops; return channel;
}

static inline void Mix_HaltChannel(int channel) { (void)channel; }
static inline void Mix_SetPanning(int channel, uint8_t left, uint8_t right) {
    (void)channel; (void)left; (void)right;
}

static inline int Mix_GroupAvailable(int tag) { (void)tag; return -1; }
static inline int Mix_GroupOldest(int tag) { (void)tag; return -1; }

static inline Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) {
    (void)src; (void)freesrc; return NULL;
}

static inline void Mix_FreeChunk(Mix_Chunk *chunk) { (void)chunk; }
static inline const char *Mix_GetError(void) { return "no audio"; }

/*==========================================================================
 * SDL_CreateWindow / CreateRenderer / CreateTexture stubs
 *==========================================================================*/

static inline SDL_Window *SDL_CreateWindow(const char *title, int x, int y,
    int w, int h, uint32_t flags) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)flags;
    return NULL;
}

static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index,
    uint32_t flags) {
    (void)window; (void)index; (void)flags;
    return NULL;
}

static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer,
    uint32_t format, int access, int w, int h) {
    (void)renderer; (void)format; (void)access; (void)w; (void)h;
    return NULL;
}

static inline void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }
static inline void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
static inline void SDL_DestroyTexture(SDL_Texture *t) { (void)t; }
static inline void SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode) { (void)r; (void)mode; }
static inline void SDL_UpdateTexture(SDL_Texture *t, void *rect, void *pixels, int pitch) {
    (void)t; (void)rect; (void)pixels; (void)pitch;
}
static inline void SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, void *src, void *dst) {
    (void)r; (void)t; (void)src; (void)dst;
}
static inline void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }

#define SDL_TEXTUREACCESS_STREAMING 1

static inline SDL_Surface *SDL_CreateRGBSurface(uint32_t flags, int w, int h,
    int depth, uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
    (void)flags; (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    s->w = w;
    s->h = h;
    int bpp = (depth + 7) / 8;
    s->pitch = w * bpp;
    s->pixels = calloc(1, s->pitch * h);
    s->format = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    s->format->BytesPerPixel = bpp;
    s->format->palette = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
    return s;
}

/*==========================================================================
 * SDL Error stubs
 *==========================================================================*/

static inline const char *SDL_GetError(void) { return ""; }

static inline int SDL_SaveBMP(SDL_Surface *surface, const char *file) {
    (void)surface; (void)file;
    return -1;
}

#define SDL_MESSAGEBOX_ERROR       0x10
#define SDL_MESSAGEBOX_INFORMATION 0x40
static inline int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title,
    const char *message, void *window) {
    (void)flags; (void)title; (void)window;
    printf("%s\n", message);
    return 0;
}

/*==========================================================================
 * Misc stubs
 *==========================================================================*/

static inline void atexit_sdl(void (*func)(void)) { (void)func; }

// Wolf3D's Error() is declared in wl_utils.h - don't redeclare here

// POSIX stubs for bare metal
int unlink(const char *path);
int mkdir(const char *path, unsigned long mode);
int open(const char *path, int flags, ...);
int close(int fd);
int read(int fd, void *buf, unsigned int count);

#endif // SDL_COMPAT_H
