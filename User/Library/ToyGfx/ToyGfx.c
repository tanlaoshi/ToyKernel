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
