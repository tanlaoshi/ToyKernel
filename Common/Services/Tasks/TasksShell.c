/*
 * TasksShell.c — Shell 任务（PR-S-tasks-1）
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "ToySerialLog.h"

/* CoolTerm 常发 CR+LF：两次 Enter → 双 toyos>；吞掉紧跟 CR 的 LF */
static int gSerialSkipLf;

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

void ShellTask(void) {
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    /*
     * 串口首提示放在进调度之后：ConsoleInitialize 过早 Prompt 会被 AP idle 抢行；
     * 又不能只靠开 GUI 窗（开窗失败/卡住时 Enter 永远无 toyos>）。
     */
    Prompt();
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
        /* 拖帧合成中后缓冲是半成品；这一刷会整块再贴，闪且卡 */
        if (!GuiPresentBlocked()) {
            HalVideoPresent();
        }

        /*
         * COM1 RX → Shell / 用户 stdin 环（CoolTerm 遥控）。
         * 真机曾 MaxRx=32：粘贴 dbset …192.168.31.124 时 HW FIFO(16) 溢掉中间字。
         * 仍设上限，避免噪声狂发时永不 Net/Halt；一行命令 ≪ 256。
         */
        {
            int n = 0;
            int MaxRx = 256;
            while (HalSerialDataReady() && n < MaxRx) {
                char C = HalSerialReadChar();
                n++;
                /*
                 * exec 中：勿喂 shell，泄进 stdin 环供 read(0)；
                 * 若直接跳过不抽 RX，CoolTerm 粘贴同样会丢字。
                 */
                if (ConsoleStdinUserHold()) {
                    ConsoleStdinPut(C);
                    continue;
                }
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
                if (TtyUiIsFocused()) {
                    TtyUiOnRxChar(C);
                    continue;
                }
                if (SerialIsEnter(C)) {
                    ConsoleOnEnterEx(1);
                } else if (C == 3) {
                    ShellOnInterrupt();
                } else if (C == '\b' || C == 127) {
                    ConsoleOnBackspaceEx(1);
                } else if (C >= 32 && C <= 126) {
                    ConsoleOnCharEx(C, 1);
                }
            }
        }
#ifdef TOY_LWIP
        /*
         * NO_SYS：Shell@AP 与 Worker@BSP 均可 LwIpService；gLwIpLock 串行。
         * 勿在 Job busy 时停泵——窗/Worker 上的 HttpGet 仍依赖 Shell 侧轮询 RX。
         */
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
