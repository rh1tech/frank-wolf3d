/*
 * ID_VL.C - Video Layer for RP2350 (HDMI output)
 *
 * Replaces the SDL-based video layer with direct HDMI framebuffer access.
 * Uses an 8-bit indexed framebuffer (320x200) in PSRAM.
 */

#include "wl_def.h"

// RP2350 platform functions (from wolf_rp2350.c)
extern uint8_t *wolf_get_screenbuffer(void);
extern SDL_Color *wolf_get_palette(void);
extern void wolf_update_screen(void);
extern void wolf_set_palette(SDL_Color *palette);

boolean  fullscreen = true;
int16_t  screenWidth = 320;
int16_t  screenHeight = 200;
int      screenBits = 8;

SDL_Surface *screen = NULL;
unsigned screenPitch;

SDL_Surface *screenBuffer = NULL;
unsigned bufferPitch;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;

int      scaleFactor;

boolean  screenfaded;
unsigned bordercolor;

uint32_t *ylookup;

SDL_Color palette1[256], palette2[256];
SDL_Color curpal[256];

#define CASSERT(x) extern int ASSERT_COMPILE[((x) != 0) * 2 - 1];
#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, SDL_ALPHA_OPAQUE}

SDL_Color gamepal[] = {
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

CASSERT(lengthof(gamepal) == 256)

//===========================================================================

void VL_Shutdown(void) {
    free(ylookup);
    free(pixelangle);
    free(wallheight);
#if defined(USE_FLOORCEILINGTEX) || defined(USE_CLOUDSKY)
    free(spanstart);
    spanstart = NULL;
#endif
    ylookup = NULL;
    pixelangle = NULL;
    wallheight = NULL;
}

void VL_SetVGAPlaneMode(void) {
    int i;

    screenWidth = 320;
    screenHeight = 200;
    screenBits = 8;
    scaleFactor = 1;

    // Use the PSRAM framebuffer from wolf_rp2350.c
    uint8_t *fb = wolf_get_screenbuffer();

    // Create a minimal screenBuffer "surface" pointing to our framebuffer
    screenBuffer = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    screenBuffer->w = screenWidth;
    screenBuffer->h = screenHeight;
    screenBuffer->pitch = screenWidth;
    screenBuffer->pixels = fb;
    screenBuffer->format = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    screenBuffer->format->BytesPerPixel = 1;
    screenBuffer->format->palette = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));

    // screen surface (same as screenBuffer for 8-bit mode)
    screen = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    screen->w = screenWidth;
    screen->h = screenHeight;
    screen->pitch = screenWidth;
    screen->pixels = fb;
    screen->format = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    screen->format->BytesPerPixel = 1;
    screen->format->palette = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));

    screenPitch = screen->pitch;
    bufferPitch = screenBuffer->pitch;

    // Set game palette
    SDL_SetPaletteColors(screen->format->palette, gamepal, 0, 256);
    SDL_SetPaletteColors(screenBuffer->format->palette, gamepal, 0, 256);
    memcpy(curpal, gamepal, sizeof(SDL_Color) * 256);
    wolf_set_palette(gamepal);

    ylookup = (uint32_t *)malloc(screenHeight * sizeof(*ylookup));
    pixelangle = (short *)malloc(screenWidth * sizeof(*pixelangle));
    wallheight = (short *)malloc(screenWidth * sizeof(*wallheight));
#if defined(USE_FLOORCEILINGTEX) || defined(USE_CLOUDSKY)
    spanstart = (int16_t *)malloc((screenHeight / 2) * sizeof(*spanstart));
#endif

    for (i = 0; i < screenHeight; i++)
        ylookup[i] = i * bufferPitch;
}

void VL_SetTextMode(void) {
    // No-op on RP2350
}

//===========================================================================
// PALETTE OPS
//===========================================================================

void VL_ConvertPalette(byte *srcpal, SDL_Color *destpal, int numColors) {
    int i;
    for (i = 0; i < numColors; i++) {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
        destpal[i].a = SDL_ALPHA_OPAQUE;
    }
}

void VL_FillPalette(int red, int green, int blue) {
    int i;
    SDL_Color pal[256];
    for (i = 0; i < 256; i++) {
        pal[i].r = red;
        pal[i].g = green;
        pal[i].b = blue;
        pal[i].a = SDL_ALPHA_OPAQUE;
    }
    VL_SetPalette(pal, true);
}

void VL_GetColor(int color, int *red, int *green, int *blue) {
    SDL_Color *col = &curpal[color];
    *red = col->r;
    *green = col->g;
    *blue = col->b;
}

void VL_SetPalette(SDL_Color *palette, bool forceupdate) {
    memcpy(curpal, palette, sizeof(SDL_Color) * 256);
    SDL_SetPaletteColors(screenBuffer->format->palette, palette, 0, 256);
    wolf_set_palette(palette);
    if (forceupdate)
        VH_UpdateScreen(screenBuffer);
}

void VL_GetPalette(SDL_Color *palette) {
    memcpy(palette, curpal, sizeof(SDL_Color) * 256);
}

void VL_FadeOut(int start, int end, int red, int green, int blue, int steps) {
    int i, j, orig, delta;
    SDL_Color *origptr, *newptr;

    red = red * 255 / 63;
    green = green * 255 / 63;
    blue = blue * 255 / 63;

    VL_GetPalette(palette1);
    memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

    for (i = 0; i < steps; i++) {
        origptr = &palette1[start];
        newptr = &palette2[start];
        for (j = start; j <= end; j++) {
            orig = origptr->r;
            delta = red - orig;
            newptr->r = orig + delta * i / steps;
            orig = origptr->g;
            delta = green - orig;
            newptr->g = orig + delta * i / steps;
            orig = origptr->b;
            delta = blue - orig;
            newptr->b = orig + delta * i / steps;
            newptr->a = SDL_ALPHA_OPAQUE;
            origptr++;
            newptr++;
        }
        SDL_Delay(8);
        VL_SetPalette(palette2, true);
    }

    VL_FillPalette(red, green, blue);
    screenfaded = true;
}

void VL_FadeIn(int start, int end, SDL_Color *palette, int steps) {
    int i, j, delta;

    VL_GetPalette(palette1);
    memcpy(palette2, palette1, sizeof(SDL_Color) * 256);

    for (i = 0; i < steps; i++) {
        for (j = start; j <= end; j++) {
            delta = palette[j].r - palette1[j].r;
            palette2[j].r = palette1[j].r + delta * i / steps;
            delta = palette[j].g - palette1[j].g;
            palette2[j].g = palette1[j].g + delta * i / steps;
            delta = palette[j].b - palette1[j].b;
            palette2[j].b = palette1[j].b + delta * i / steps;
            palette2[j].a = SDL_ALPHA_OPAQUE;
        }
        SDL_Delay(8);
        VL_SetPalette(palette2, true);
    }

    VL_SetPalette(palette, true);
    screenfaded = false;
}

//===========================================================================
// PIXEL OPS
//===========================================================================

byte *VL_LockSurface(SDL_Surface *surface) {
    return (byte *)surface->pixels;
}

void VL_UnlockSurface(SDL_Surface *surface) {
    (void)surface;
}

void VL_Plot(int x, int y, int color) {
    assert(x >= 0 && x < screenWidth && y >= 0 && y < screenHeight);
    ((byte *)screenBuffer->pixels)[ylookup[y] + x] = color;
}

byte VL_GetPixel(int x, int y) {
    assert(x >= 0 && x < screenWidth && y >= 0 && y < screenHeight);
    return ((byte *)screenBuffer->pixels)[ylookup[y] + x];
}

void VL_Hlin(int x, int y, int width, int color) {
    assert(x >= 0 && x + width <= screenWidth && y >= 0 && y < screenHeight);
    memset((byte *)screenBuffer->pixels + ylookup[y] + x, color, width);
}

void VL_Vlin(int x, int y, int height, int color) {
    assert(x >= 0 && x < screenWidth && y >= 0 && y + height <= screenHeight);
    byte *dest = (byte *)screenBuffer->pixels + ylookup[y] + x;
    while (height--) {
        *dest = color;
        dest += bufferPitch;
    }
}

void VL_Bar(int x, int y, int width, int height, int color) {
    VL_BarScaledCoord(scaleFactor * x, scaleFactor * y,
                       scaleFactor * width, scaleFactor * height, color);
}

void VL_BarScaledCoord(int scx, int scy, int scwidth, int scheight, int color) {
    byte *dest = (byte *)screenBuffer->pixels + ylookup[scy] + scx;
    while (scheight--) {
        memset(dest, color, scwidth);
        dest += bufferPitch;
    }
}

//===========================================================================
// MEMORY OPS
//===========================================================================

void VL_DePlaneVGA(byte *source, int width, int height) {
    int x, y, plane;
    word size, pwidth;
    byte *temp, *dest, *srcline;

    size = width * height;
    if (width & 3)
        Quit("DePlaneVGA: width not divisible by 4!");

    temp = (byte *)malloc(size);

    srcline = source;
    pwidth = width >> 2;

    for (plane = 0; plane < 4; plane++) {
        dest = temp;
        for (y = 0; y < height; y++) {
            for (x = 0; x < pwidth; x++)
                *(dest + (x << 2) + plane) = *srcline++;
            dest += width;
        }
    }

    memcpy(source, temp, size);
    free(temp);
}

void VL_MemToScreen(byte *source, int width, int height, int x, int y) {
    VL_MemToScreenScaledCoord(source, width, height, scaleFactor * x, scaleFactor * y);
}

void VL_MemToScreenScaledCoord(byte *source, int width, int height, int destx, int desty) {
    byte *dest;
    int i, j, sci, scj;
    int m, n;

    dest = (byte *)screenBuffer->pixels;

    for (j = 0, scj = 0; j < height; j++, scj += scaleFactor) {
        for (i = 0, sci = 0; i < width; i++, sci += scaleFactor) {
            byte col = source[(j * width) + i];
            for (m = 0; m < scaleFactor; m++) {
                for (n = 0; n < scaleFactor; n++) {
                    dest[ylookup[scj + m + desty] + sci + n + destx] = col;
                }
            }
        }
    }
}

void VL_MemToScreenScaledCoord2(byte *source, int origwidth, int srcx, int srcy,
                                int destx, int desty, int width, int height) {
    byte *dest;
    int i, j, sci, scj;
    int m, n;

    dest = (byte *)screenBuffer->pixels;

    for (j = 0, scj = 0; j < height; j++, scj += scaleFactor) {
        for (i = 0, sci = 0; i < width; i++, sci += scaleFactor) {
            byte col = source[((j + srcy) * origwidth) + (i + srcx)];
            for (m = 0; m < scaleFactor; m++) {
                for (n = 0; n < scaleFactor; n++) {
                    dest[ylookup[scj + m + desty] + sci + n + destx] = col;
                }
            }
        }
    }
}

void VL_ScreenToScreen(SDL_Surface *source, SDL_Surface *dest) {
    SDL_BlitSurface(source, NULL, dest, NULL);
}
