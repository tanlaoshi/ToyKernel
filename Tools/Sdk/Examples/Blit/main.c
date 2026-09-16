/*
 * Examples/Blit — ToyGfxDamageRect 像素色块（PR-A-examples）
 * 产物：BLIT.ELF。单次矩形 ≤ 64×64。
 */
#include <stdio.h>
#include <unistd.h>
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
    ToyGfxDamageText(Wid, "pixel blit (close to exit)");

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
    printf("blit: wid=%d ok\n", Wid);

    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
        toy_yield();
    }
    return 0;
}
