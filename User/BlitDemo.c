/*
 * BlitDemo.c — PR-G-desk-3：ToyGfxDamageRect 像素 blit 演示
 * Shell：exec BLITDEMO.ELF → 窗内画色块 → 点 × 退出
 */
#include <stdio.h>
#include <unistd.h>
#include <ToyGfx.h>
#include <ToyUi.h>
#include <toyos/syscall.h>

#define BLIT_W 48u
#define BLIT_H 48u

int main(void) {
    int WindowId;
    int Event;
    unsigned Pix[BLIT_W * BLIT_H];
    unsigned Row;
    unsigned Col;
    TOY_GFX_DAMAGE_RECT Desc;

    WindowId = ToyUiCreateWindow("BlitDemo", 420, 260);
    if (WindowId < 0) {
        printf("blitdemo: create fail\n");
        return 1;
    }
    if (ToyGfxDamageText(WindowId, "pixel blit (close window to exit)") != 0) {
        printf("blitdemo: text fail\n");
        return 1;
    }

    for (Row = 0; Row < BLIT_H; Row++) {
        for (Col = 0; Col < BLIT_W; Col++) {
            unsigned R = (Col * 255u) / (BLIT_W - 1u);
            unsigned G = (Row * 255u) / (BLIT_H - 1u);
            unsigned B = 64u;
            Pix[Row * BLIT_W + Col] = (R << 16) | (G << 8) | B;
        }
    }
    Desc.X = 16;
    Desc.Y = 32;
    Desc.W = BLIT_W;
    Desc.H = BLIT_H;
    Desc.Pixels = Pix;
    if (ToyGfxDamageRect(WindowId, &Desc) != 0) {
        printf("blitdemo: damage_rect fail\n");
        return 1;
    }

    printf("blitdemo: wid=%d blit ok\n", WindowId);
    for (;;) {
        Event = ToyUiPoll(WindowId);
        if (Event < 0) {
            printf("blitdemo: poll fail\n");
            return 1;
        }
        if (Event == TOY_UI_EVENT_CLOSE) {
            break;
        }
        toy_yield();
    }
    printf("blitdemo: closed ok\n");
    return 0;
}
