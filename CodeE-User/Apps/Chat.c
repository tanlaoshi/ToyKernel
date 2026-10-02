/* Chat.c — GUI 半双工聊天（壳式 chat>，无 TextField/按钮）
 * 客户区只用 DamageText（系统字），交互仿 Shell 行编辑。
 */
#include "ChatNet.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <ToyNet.h>
#include <ToyUi.h>

#define CHAT_LOG_MAX  4
#define CHAT_LOG_COLS 28
#define CHAT_DRAFT_MAX 48

static char gLog[CHAT_LOG_MAX][CHAT_LOG_COLS];
static int gLogN;
static int gWid = -1;
static int gSock = -1;
static int gWaitPeer;
static char gDraft[CHAT_DRAFT_MAX];
static int gDraftN;
static char gRx[CHAT_LINE_MAX + 4];
static int gRxN;

/* USB HID → ASCII（无 Shift；与 ToyUi 子集对齐） */
static char HidAscii(int Hid) {
    if (Hid >= 0x04 && Hid <= 0x1D) {
        return (char)('a' + (Hid - 0x04));
    }
    if (Hid >= 0x1E && Hid <= 0x26) {
        return (char)('1' + (Hid - 0x1E));
    }
    if (Hid == 0x27) {
        return '0';
    }
    if (Hid == 0x2C) {
        return ' ';
    }
    if (Hid == 0x37) {
        return '.';
    }
    if (Hid == 0x36) {
        return ',';
    }
    if (Hid == 0x2D) {
        return '-';
    }
    if (Hid == 0x38) {
        return '/';
    }
    return 0;
}

static void LogPush(const char *Who, const char *Msg) {
    char Line[CHAT_LOG_COLS];
    int I;
    int N = 0;

    while (Who[N] && N < 4) {
        Line[N] = Who[N];
        N++;
    }
    Line[N++] = ' ';
    I = 0;
    while (Msg[I] && N + 1 < CHAT_LOG_COLS) {
        Line[N++] = Msg[I++];
    }
    Line[N] = 0;
    if (gLogN >= CHAT_LOG_MAX) {
        for (I = 1; I < CHAT_LOG_MAX; I++) {
            memcpy(gLog[I - 1], gLog[I], CHAT_LOG_COLS);
        }
        gLogN = CHAT_LOG_MAX - 1;
    }
    memcpy(gLog[gLogN], Line, CHAT_LOG_COLS);
    gLogN++;
}

/* 仿 Shell：日志 + 当前「chat> 草稿」画在客户区（≤ ClientText 128） */
static void UiPaint(void) {
    char Buf[128];
    int I;
    int Off = 0;

    for (I = 0; I < gLogN && Off + 2 < (int)sizeof(Buf); I++) {
        int J = 0;
        if (I > 0) {
            Buf[Off++] = '\n';
        }
        while (gLog[I][J] && Off + 1 < (int)sizeof(Buf)) {
            Buf[Off++] = gLog[I][J++];
        }
    }
    if (Off + 2 < (int)sizeof(Buf)) {
        if (Off > 0) {
            Buf[Off++] = '\n';
        }
        if (gWaitPeer) {
            const char *H = "…waiting peer";
            I = 0;
            while (H[I] && Off + 1 < (int)sizeof(Buf)) {
                Buf[Off++] = H[I++];
            }
        } else {
            const char *P = "chat> ";
            I = 0;
            while (P[I] && Off + 1 < (int)sizeof(Buf)) {
                Buf[Off++] = P[I++];
            }
            I = 0;
            while (I < gDraftN && Off + 1 < (int)sizeof(Buf)) {
                Buf[Off++] = gDraft[I++];
            }
        }
    }
    Buf[Off] = 0;
    if (gWid >= 0) {
        ToyUiSetLabel(gWid, Buf[0] ? Buf : "chat>");
    }
}

static int TrySendDraft(void) {
    if (gWaitPeer || gSock < 0) {
        return 0;
    }
    if (gDraftN == 0) {
        return 0;
    }
    gDraft[gDraftN] = 0;
    if (strcmp(gDraft, "/quit") == 0) {
        return -2;
    }
    if (ChatSendLine(gSock, gDraft, (size_t)gDraftN) != 0) {
        LogPush("--", "send fail");
        gDraftN = 0;
        UiPaint();
        return -1;
    }
    LogPush("me>", gDraft);
    gDraftN = 0;
    gWaitPeer = 1;
    gRxN = 0;
    UiPaint();
    return 0;
}

static void OnKey(int Hid) {
    char C;

    if (gWaitPeer) {
        return;
    }
    if (Hid == 0x2A) { /* Backspace */
        if (gDraftN > 0) {
            gDraftN--;
            UiPaint();
        }
        return;
    }
    C = HidAscii(Hid);
    if (!C) {
        return;
    }
    if (gDraftN + 1 >= CHAT_DRAFT_MAX) {
        return;
    }
    gDraft[gDraftN++] = C;
    UiPaint();
}

/* 1=完成 0=继续 -1=错 -2=EOF */
static int PollPeerLine(void) {
    for (;;) {
        char C;
        ssize_t R = recv(gSock, &C, 1, MSG_DONTWAIT);
        if (R < 0) {
            if (errno == EAGAIN) {
                return 0;
            }
            return -1;
        }
        if (R == 0) {
            return -2;
        }
        if (C == '\n') {
            if (gRxN > 0 && gRx[gRxN - 1] == '\r') {
                gRx[--gRxN] = 0;
            }
            gRx[gRxN] = 0;
            if (gRxN == 0) {
                continue;
            }
            LogPush("peer", gRx);
            gWaitPeer = 0;
            gRxN = 0;
            UiPaint();
            return 1;
        }
        if (gRxN + 1 < (int)sizeof(gRx) && gRxN < CHAT_LINE_MAX) {
            gRx[gRxN++] = C;
        }
    }
}

int main(void) {
    unsigned Ip;
    unsigned short Port;
    int Ev;

    if (ChatLoadPeer(&Ip, &Port) != 0) {
        return 1;
    }
    printf("chat: peer %u.%u.%u.%u:%u\n",
           (Ip >> 24) & 0xffu, (Ip >> 16) & 0xffu,
           (Ip >> 8) & 0xffu, Ip & 0xffu, (unsigned)Port);

    if (ToyGfxInitialize() != 0) {
        printf("chat: gfx init fail\n");
        return 1;
    }
    /* 无底栏按钮：客户区整页给日志 + chat> */
    gWid = ToyUiCreateWindow("Chat", 420, 260);
    if (gWid < 0) {
        printf("chat: window fail\n");
        return 1;
    }

    gSock = socket(AF_INET, SOCK_STREAM, 0);
    if (gSock < 0) {
        LogPush("--", "socket fail");
        UiPaint();
        return 1;
    }
    LogPush("--", "connecting…");
    UiPaint();
    if (ToyNetConnect(gSock, Ip, Port) != 0) {
        LogPush("--", "connect fail");
        UiPaint();
        close(gSock);
        return 1;
    }
    LogPush("--", "connected");
    gDraftN = 0;
    UiPaint();

    for (;;) {
        if (gWaitPeer) {
            int Rc = PollPeerLine();
            if (Rc == -2) {
                LogPush("--", "peer closed");
                UiPaint();
                break;
            }
            if (Rc < 0) {
                LogPush("--", "recv fail");
                UiPaint();
                break;
            }
        }
        Ev = ToyUiPoll(gWid);
        if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (TOY_UI_IS_KEY(Ev)) {
            int Hid = TOY_UI_KEY_CODE(Ev);
            if (Hid == 0x28) {
                int Rc = TrySendDraft();
                if (Rc == -2 || Rc < 0) {
                    break;
                }
            } else {
                OnKey(Hid);
            }
        }
        usleep(5000);
    }
    if (gSock >= 0) {
        close(gSock);
    }
    return 0;
}
