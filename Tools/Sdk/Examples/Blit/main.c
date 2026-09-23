/*
 * Examples/Blit — ToyGfxDamageRect + 点/线/矩形封装（PR-A-gfx-api）
 * 产物：BLIT.ELF。单次 blit ≤ 64×64；FillRect 内部会分块。
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <ToyGfx.h>
#include <ToyUi.h>
#include <toyos/syscall.h>

#define BLIT_W 32u
#define BLIT_H 32u

int main(void) {
    int Wid;
    int Ev;
    unsigned Pix[BLIT_W * BLIT_H];
    unsigned I;
    TOY_GFX_DAMAGE_RECT Desc;

    Wid = ToyUiCreateWindow("Blit", 320, 200);
    if (Wid < 0) {
        printf("blit: create fail\n");
        return 1;
    }
    ToyGfxDamageText(Wid, "pixel blit + fill/line (close to exit)");

    for (I = 0; I < BLIT_W * BLIT_H; I++) {
        Pix[I] = 0x00C04040u;
    }
    Desc.X = 16;
    Desc.Y = 24;
    Desc.W = BLIT_W;
    Desc.H = BLIT_H;
    Desc.Pixels = Pix;
    if (ToyGfxDamageRect(Wid, &Desc) != 0) {
        printf("blit: damage_rect fail\n");
        return 1;
    }
    if (ToyGfxFillRect(Wid, 56, 24, 48, 20, 0x004080C0u) != 0) {
        printf("blit: fill fail\n");
        return 1;
    }
    if (ToyGfxDrawRect(Wid, 140, 24, 48, 40, 0x00E0E0E0u) != 0) {
        printf("blit: rect fail\n");
        return 1;
    }
    if (ToyGfxDrawLine(Wid, 16, 80, 200, 100, 0x00E0C040u) != 0) {
        printf("blit: line fail\n");
        return 1;
    }
    if (ToyGfxDrawPixel(Wid, 24, 88, TOY_GFX_COLOR_WHITE) != 0) {
        printf("blit: pixel fail\n");
        return 1;
    }
    printf("blit: wid=%d ok (ToyGfx %s)\n", Wid, TOY_GFX_ABI_VERSION_STRING);

    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
        sched_yield();
    }
    return 0;
}
