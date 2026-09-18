/*
 * Tasks.c — 内核常驻任务：Shell / GUI / Worker
 */
#include "Tasks.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "ToySerialLog.h"

static volatile UINT32 gWorkerCount;
/* CoolTerm 常发 CR+LF：两次 Enter → 双 toyos>；吞掉紧跟 CR 的 LF */
static int gSerialSkipLf;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
}

static int SerialIsEnter(char C) {
    if (C == '\r') {
        gSerialSkipLf = 1;
        return 1;
    }
    if (C == '\n') {
        if (gSerialSkipLf) {
            gSerialSkipLf = 0;
            return 0;
        }
        return 1;
    }
    gSerialSkipLf = 0;
    return 0;
}

/*
 * 真机 poll-USB：纯 hlt 要等 PIT/HPET tick 才醒 → 光标更新锁在 ~10ms+，体感极卡。
 * 多数轮次短自旋；偶发 hlt 仍给定时器/短按电源窗口。
 */
static void YieldForPollInput(void) {
    /*
     * PR-S-input-drain（序 1 等价）/ PR-S-input-pin（序 2）：
     * - SMP=2（无 InputTask）：此处为稳态 drain 主人（shell/gui 每轮让步前排空 xHCI 事件环）。
     * - SMP≥3（InputTask 钉 CPU2 持续 drain）：此处降为兜底——InputTask 为主人，本路径冗余但
     *   无害（gEvtConsumerLock 串行）；保留以复用单/双核路径，勿分支化。
     * 长 Store IO 不走此路径，由 StoreIoBreath 自带 drain 兜底。
     */
    HalInputPoll();
    if (!HalCpuIsHypervisor()) {
        UINT32 i;
        if (HalPowerButtonPressed()) {
            ToyLogBoot("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        /* 真机 poll-USB：勿 hlt 等 tick，否则光标锁 ~10ms+ */
        for (i = 0; i < 200; i++) {
            __asm__ volatile ("pause");
        }
        return;
    }
    HalCpuHalt();
}

static void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous) {
    for (int i = 0; i < 6; i++) {
        UINT8 Key = Report->KeyCode[i];
        if (Key == 0) {
            continue;
        }
        int WasDown = 0;
        for (int j = 0; j < 6; j++) {
            if (Previous->KeyCode[j] == Key) {
                WasDown = 1;
                break;
            }
        }
        if (WasDown) {
            continue;
        }

        /* Settings 文字菜单：数字选择 / Esc 返回 */
        if (SettingsUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                SettingsUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
                Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                GuiOnArrowKey(Key);
                continue;
            }
            {
                char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
                if (C >= '0' && C <= '9') {
                    SettingsUiOnDigit(C);
                }
            }
            continue;
        }

        /* Edit：文本编辑 / Ctrl+S 保存 */
        if (EditUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                EditUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_ENTER) {
                EditUiOnEnter();
                continue;
            }
            if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                EditUiOnArrow(Key == HID_KEY_DOWN);
                continue;
            }
            if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT) {
                EditUiOnArrowLeftRight(Key == HID_KEY_RIGHT);
                continue;
            }
            if (Key == HID_KEY_BACKSPACE) {
                EditUiOnBackspace();
                continue;
            }
            if (Key == HID_KEY_DELETE) {
                EditUiOnDeleteKey();
                continue;
            }
            if (Key == HID_KEY_S &&
                (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
                EditUiSave();
                continue;
            }
            if (Key == HID_KEY_CAPSLOCK) {
                HIDKeyboardToggleCapsLock();
                HalKeyboardSetLeds(HIDKeyboardGetLeds());
                continue;
            }
            {
                char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
                if (C != 0) {
                    EditUiOnChar(C);
                }
            }
            continue;
        }

        /* Files：导航 / 写操作快捷键 / 确认与输入 */
        if (FilesUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                FilesUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_ENTER) {
                FilesUiOnEnter();
                continue;
            }
            if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                FilesUiOnArrow(Key == HID_KEY_DOWN);
                continue;
            }
            if (Key == HID_KEY_BACKSPACE) {
                FilesUiOnBackspace();
                continue;
            }
            if (Key == HID_KEY_DELETE) {
                FilesUiOnDeleteKey();
                continue;
            }
            if (Key == HID_KEY_CAPSLOCK) {
                HIDKeyboardToggleCapsLock();
                HalKeyboardSetLeds(HIDKeyboardGetLeds());
                continue;
            }
            {
                char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
                if (C != 0) {
                    FilesUiOnChar(C);
                }
            }
            continue;
        }

        if (Key == HID_KEY_ENTER) {
            /* ConsoleOnEnter → EnsureShell：空桌面时开 Shell */
            ConsoleOnEnter();
            continue;
        }
        if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
            Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
            GuiOnArrowKey(Key);
            continue;
        }
        if (Key == HID_KEY_BACKSPACE) {
            if (GuiShellAcceptsInput()) {
                ConsoleOnBackspace();
            }
            continue;
        }
        if (Key == HID_KEY_CAPSLOCK) {
            HIDKeyboardToggleCapsLock();
            HalKeyboardSetLeds(HIDKeyboardGetLeds());
            continue;
        }
        if (Key == HID_KEY_C &&
            (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
            ShellOnInterrupt();
            continue;
        }

        char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (C != 0) {
            ConsoleOnChar(C);
        }
    }
}

void GuiTask(void) {
    for (;;) {
        GuiPollMouse();
        YieldForPollInput();
    }
}

void ShellTask(void) {
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    for (;;) {
        /*
         * 真机 xHCI 为 poll（无 MSI）：必须先 Drain/取键再 hlt。
         * 旧序先 Halt → 仅靠定时器偶发唤醒，事件环易在 gui 启动后溢满假死。
         * PR-S-input-drain：drain 已移交 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）；
         * 此处只 dequeue。
         */
        while (HalKeyboardDequeue(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        GuiPollMouse();
        /*
         * PR-G-shell-present：打字回显经 EchoMark 跳过逐键 Present，
         * 本处合并提交脏区（亦续传上次半途 gDirty）。virt/真机同路径。
         */
        HalVideoPresent();

        /*
         * COM1 RX → Shell（CoolTerm 遥控打字）；TX 仍是调试旁路。
         * 每轮最多收 N 字节，然后继续 Net/Halt——勿 while 抽干，
         * 否则对端狂发/噪声时永不 hlt → USB 键失效、短按电源无效。
         */
        {
            int n = 0;
            int MaxRx = HalCpuIsHypervisor() ? 256 : 32;
            while (HalSerialDataReady() && n < MaxRx) {
                char C = HalSerialReadChar();
                n++;
                if (SettingsUiIsFocused()) {
                    if (C == 0x1B) {
                        SettingsUiOnEscape();
                    } else if (C >= '0' && C <= '9') {
                        SettingsUiOnDigit(C);
                    }
                    continue;
                }
                if (FilesUiIsFocused()) {
                    if (C == 0x1B) {
                        FilesUiOnEscape();
                    } else if (SerialIsEnter(C)) {
                        FilesUiOnEnter();
                    } else if (C == '\b' || C == 127) {
                        FilesUiOnBackspace();
                    } else if (C >= 32 && C <= 126) {
                        FilesUiOnChar(C);
                    }
                    continue;
                }
                if (EditUiIsFocused()) {
                    if (C == 0x1B) {
                        EditUiOnEscape();
                    } else if (SerialIsEnter(C)) {
                        EditUiOnEnter();
                    } else if (C == '\b' || C == 127) {
                        EditUiOnBackspace();
                    } else if (C == 19) {
                        EditUiSave();
                    } else if (C >= 32 && C <= 126) {
                        EditUiOnChar(C);
                    }
                    continue;
                }
                if (SerialIsEnter(C)) {
                    ConsoleOnEnter();
                } else if (C == 3) {
                    ShellOnInterrupt();
                } else if (C == '\b' || C == 127) {
                    ConsoleOnBackspace();
                } else if (C >= 32 && C <= 126) {
                    ConsoleOnChar(C);
                }
            }
        }
#ifdef TOY_LWIP
        if (LwIpActive()) {
            LwIpService();
        } else {
            HalNetPoll();
            TcpPoll();
        }
#else
        HalNetPoll();
        TcpPoll();
#endif
        if (HalPowerButtonPressed()) {
            ToyLogBoot("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        {
            UDP_DATAGRAM Dg;
            int (*RecvFn)(UDP_DATAGRAM *) = UdpRecv;
#ifdef TOY_LWIP
            if (LwIpActive()) {
                RecvFn = LwIpUdpRecv;
            }
#endif
            while (RecvFn(&Dg)) {
                char IpBuf[20];
                UINTN i;
                HalNetFormatIp(Dg.SrcIp, IpBuf, sizeof(IpBuf));
                ConsoleWrite("udp from ");
                ConsoleWrite(IpBuf);
                ConsoleWrite(":");
                ConsoleWriteHex32(Dg.SrcPort);
                ConsoleWrite(" ");
                for (i = 0; i < Dg.Len; i++) {
                    char C = (char)Dg.Data[i];
                    if (C >= 32 && C <= 126) {
                        ConsoleOnChar(C);
                    }
                }
                ConsoleWrite("\n");
            }
        }
        YieldForPollInput();
    }
}

void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        for (volatile int i = 0; i < 5000; i++) {
        }
        HalCpuHalt();
    }
}

/*
 * PR-S-input-pin（序 2）：输入钉专核（SMP≥3 → 逻辑 CPU2）。
 *
 * scoped sti（关键）：sti 只在浅栈的 200-pause 期开；HalInputPoll 期 cli 关中断。
 * 原因：HalInputPoll → XchiDrainEvents → ProcessEvents* 调用链深，若被定时器在
 * 深调用中抢占，ISR 链（InterruptDispatch→SchedulerOnTimer→PickNext）压在深栈之上，
 * 实测会损坏 iret 帧（rip/rsp 变 gTasks 栈址）→ #UD/#GP。worker 之所以稳，是它只在
 * hlt（浅栈）被抢。故 InputTask 须只在浅 pause 处被抢：cli 守 drain，sti 放 pause。
 * 其专核上只有 InputTask+idle，sti 不外溢到 shell/gui 协作态（CPU1 仍 IF=0）。
 *
 * 并发安全：XchiDrainEvents 已 gEvtConsumerLock/gHidQueueLock 串行 + 命令期
 * XhciEventIsExclusive 早退 → 与 shell/gui 的 yield-path drain 并发安全（序 1 已证）。
 */
void InputTask(void) {
    for (;;) {
        __asm__ volatile ("cli");   /* drain 期关中断：勿在深调用中被抢 */
        HalInputPoll();
        __asm__ volatile ("sti");   /* pause 期开中断：浅栈处可被定时器抢 → ticks>0 */
        {
            UINT32 i;
            for (i = 0; i < 200; i++) {
                __asm__ volatile ("pause");
            }
        }
    }
}
