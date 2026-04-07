// WL_UTILS.C

#include "wl_utils.h"


/*
===================
=
= safe_malloc
=
= Wrapper for malloc with a NULL check
=
===================
*/

#ifdef PICO_ON_DEVICE
#include "psram_allocator.h"

// PSRAM address range: 0x11000000 - 0x11800000 (8 MB)
#define IS_PSRAM_PTR(p) ((uintptr_t)(p) >= 0x11000000 && (uintptr_t)(p) < 0x11800000)

void *safe_malloc (size_t size, const char *fname, uint32_t line)
{
    void *ptr = psram_malloc(size);
    if (!ptr)
        Quit ("SafeMalloc: Out of memory at %s: line %u",fname,line);
    return ptr;
}

// Undefine the macro so we can call the real free()
#undef free

// safe_free: handles both PSRAM and SRAM pointers
void safe_free(void *ptr) {
    if (!ptr) return;
    if (IS_PSRAM_PTR(ptr))
        psram_free(ptr);
    else
        free(ptr);
}

// Re-enable the macro for the rest of this file
#define free(p) safe_free(p)

#else
void *safe_malloc (size_t size, const char *fname, uint32_t line)
{
    void *ptr = malloc(size);
    if (!ptr)
        Quit ("SafeMalloc: Out of memory at %s: line %u",fname,line);
    return ptr;
}
#endif


fixed FixedMul (fixed a, fixed b)
{
	return (fixed)(((int64_t)a * b + 0x8000) >> FRACBITS);
}

fixed FixedDiv (fixed a, fixed b)
{
	int64_t c = ((int64_t)a << FRACBITS) / (int64_t)b;

	return (fixed)c;
}

uint16_t ReadShort (void *ptr)
{
    unsigned value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8);

    return value;
}

uint32_t ReadLong (void *ptr)
{
    uint32_t value;
    byte     *work;

    work = ptr;
    value = work[0] | (work[1] << 8) | (work[2] << 16) | (work[3] << 24);

    return value;
}


void Error (const char *string)
{
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR,"Wolf4SDL",string,NULL);
}

void Help (const char *string)
{
    SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_INFORMATION,"Wolf4SDL",string,NULL);
}
