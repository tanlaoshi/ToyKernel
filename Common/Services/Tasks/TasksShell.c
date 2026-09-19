/*
 * TasksShell.c — Shell 任务（PR-S-tasks-1）
 */
#include "Tasks.h"
#include "TasksPrivate.h"
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
