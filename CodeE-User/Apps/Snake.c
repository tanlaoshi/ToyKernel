/*
 * Snake.c — 教学贪吃蛇（方向键或 WASD；节拍用 msleep）
 * Shell：exec SNAKE.ELF
 */
#include <ToyUi.h>
#include <stdio.h>
#include <unistd.h>

#define COLS 16
#define ROWS 12
#define CELL 12
#define PAD 10
#define BOARD_W (COLS * CELL)
#define BOARD_H (ROWS * CELL)
#define OX PAD
#define OY PAD
/*
 * create_window 的宽高是外框。可画客户区 =
 *   W - 2(边框) - 2*GUI_CLIENT_PAD(8)，H - TITLE(40) - 1 - 2*PAD。
 * 外框须预留这些，否则棋盘右侧会被裁掉约半格。
 */
#define FRAME_X (2 + 8 * 2)
#define FRAME_Y (40 + 1 + 8 * 2)
#define WIN_W (BOARD_W + PAD * 2 + FRAME_X)
#define WIN_H (BOARD_H + PAD * 2 + FRAME_Y)
#define SNAKE_MAX 64

#define COL_BOARD 0x00101820u
#define COL_BODY  0x0040C080u
#define COL_HEAD  0x00E8F8FFu
#define COL_FOOD  0x00E04040u
#define COL_DEAD  0x00802020u

static int Gx[SNAKE_MAX];
static int Gy[SNAKE_MAX];
static int Glen;
static int Gdir;
static int Gnext;
static int Fx;
static int Fy;
static int Galive;
static int Ggo;

static const int Gdx[4] = { 1, 0, -1, 0 };
static const int Gdy[4] = { 0, 1, 0, -1 };

static int OnSnake(int X, int Y, int SkipTail) {
    int N = Glen;
    int I;

    if (SkipTail && N > 0) {
        N--;
    }
    for (I = 0; I < N; I++) {
        if (Gx[I] == X && Gy[I] == Y) {
            return 1;
        }
    }
    return 0;
}

static void DrawCell(int Wid, int X, int Y, unsigned Color) {
    ToyGfxFillRect(Wid, OX + (unsigned)X * CELL, OY + (unsigned)Y * CELL,
                   CELL - 1, CELL - 1, Color);
}

static void PlaceFood(void) {
    unsigned Guard;
    unsigned Slot;

    Slot = (unsigned)clock_ms();
    for (Guard = 0; Guard < (unsigned)(COLS * ROWS); Guard++) {
        int X = (int)((Slot + Guard) % COLS);
        int Y = (int)(((Slot / COLS) + Guard / COLS) % ROWS);

        if (!OnSnake(X, Y, 0)) {
            Fx = X;
            Fy = Y;
            return;
        }
    }
    Fx = -1;
}

static void DrawBoard(int Wid) {
    int I;

    ToyGfxFillRect(Wid, OX, OY, COLS * CELL, ROWS * CELL, COL_BOARD);
    for (I = 0; I < Glen; I++) {
        DrawCell(Wid, Gx[I], Gy[I], I == 0 ? COL_HEAD : COL_BODY);
    }
    if (Fx >= 0) {
        DrawCell(Wid, Fx, Fy, COL_FOOD);
    }
}

static void ResetSnake(int Wid) {
    Glen = 3;
    Gx[0] = 8;
    Gy[0] = 6;
    Gx[1] = 7;
    Gy[1] = 6;
    Gx[2] = 6;
    Gy[2] = 6;
    Gdir = 0;
    Gnext = 0;
    Galive = 1;
    Ggo = 0;
    PlaceFood();
    DrawBoard(Wid);
}

static void Turn(int Dir) {
    if (Dir < 0 || Dir > 3) {
        return;
    }
    if (Ggo && ((Dir + 2) & 3) == Gdir) {
        return;
    }
    Gnext = Dir;
    Gdir = Dir;
    Ggo = 1;
}

static void OnKey(int Hid) {
    if (Hid == 0x4F || Hid == 0x07) {
        Turn(0);
    } else if (Hid == 0x51 || Hid == 0x16) {
        Turn(1);
    } else if (Hid == 0x50 || Hid == 0x04) {
        Turn(2);
    } else if (Hid == 0x52 || Hid == 0x1A) {
        Turn(3);
    }
}

static int Step(int Wid) {
    int Nx;
    int Ny;
    int Eat;
    int I;

    Gdir = Gnext;
    Nx = Gx[0] + Gdx[Gdir];
    Ny = Gy[0] + Gdy[Gdir];
    if (Nx < 0 || Ny < 0 || Nx >= COLS || Ny >= ROWS) {
        return 0;
    }
    Eat = (Nx == Fx && Ny == Fy);
    if (OnSnake(Nx, Ny, Eat ? 0 : 1)) {
        return 0;
    }
    if (Eat) {
        if (Glen >= SNAKE_MAX) {
            return 0;
        }
        Glen++;
    }
    for (I = Glen - 1; I > 0; I--) {
        Gx[I] = Gx[I - 1];
        Gy[I] = Gy[I - 1];
    }
    Gx[0] = Nx;
    Gy[0] = Ny;
    if (Eat) {
        PlaceFood();
    }
    DrawBoard(Wid);
    return 1;
}

static int WaitTick(int Wid) {
    unsigned long Deadline;
    int K;

    /*
     * clock_ms / msleep 已按 HalTicksPerSec 对齐墙钟；约 500ms 一格。
     * 短睡勤 poll，键经 TasksPumpKeyboard 入队。
     */
    Deadline = clock_ms() + 500ul;
    for (K = 0; K < 20000; K++) {
        int Ev = ToyUiPoll(Wid);

        if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
            return -1;
        }
        if (TOY_UI_IS_KEY(Ev)) {
            OnKey(TOY_UI_KEY_CODE(Ev));
            if (!Galive) {
                return 2;
            }
        }
        if (clock_ms() >= Deadline) {
            break;
        }
        msleep(1);
    }
    return 0;
}

int main(void) {
    int Wid;
    int St;

    Wid = ToyUiCreateWindow("Snake", WIN_W, WIN_H);
    if (Wid < 0) {
        printf("snake: create fail\n");
        return 1;
    }
    ResetSnake(Wid);
    printf("snake: arrows or wasd\n");
    for (;;) {
        St = WaitTick(Wid);
        if (St < 0) {
            break;
        }
        if (!Galive) {
            if (St == 2) {
                ResetSnake(Wid);
            }
            continue;
        }
        if (!Ggo) {
            continue;
        }
        if (!Step(Wid)) {
            Galive = 0;
            DrawCell(Wid, Gx[0], Gy[0], COL_DEAD);
            printf("snake: dead len=%d\n", Glen);
        }
    }
    printf("snake: closed\n");
    return 0;
}
