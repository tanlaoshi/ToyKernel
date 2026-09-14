/*
 * ToyGfx.c — libToyGfx
 */
#include <ToyGfx.h>
#include <toyos/syscall.h>

int ToyGfxDamageText(int WindowId, const char *Text) {
    if (WindowId < 0 || !Text) {
        return -1;
    }
    return (int)toy_damage(WindowId, Text);
}

int ToyGfxDamageRect(int WindowId, const TOY_GFX_DAMAGE_RECT *Desc) {
    unsigned long long N;

    if (WindowId < 0 || !Desc || !Desc->Pixels) {
        return -1;
    }
    if (Desc->W == 0 || Desc->H == 0) {
        return -1;
    }
    N = (unsigned long long)Desc->W * (unsigned long long)Desc->H;
    if (N == 0 || N > (unsigned long long)TOY_GFX_DAMAGE_RECT_MAX_PIXELS) {
        return -1;
    }
    return (int)toy_damage_rect(WindowId, Desc);
}
