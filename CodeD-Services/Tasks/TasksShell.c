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

void ShellTask(void) {
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    /*
     * 串口 ready/toyos> 由 Worker 在 iwl + 首轮 DHCP 结束后 Announce。
     * 此处勿 Prompt：否则夹在 Boot: iwl8265 / dhcp 中间。
     */
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
         * COM1 RX → Shell / 用户 stdin 环（CoolTerm 遥控）。
         * 必须在 HalVideoPresent 之前抽：Present 可达数 ms，16550 FIFO(16)
         * 在 115200 下约 1.4ms 就满；旧序 Present→RX 时粘贴 dbset 必截断。
         * MaxRx 仍限一轮喂 shell 量；HW→软环由 SerialRxPump（定时器）兜底。
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
                    } else if (C == '\r' || (C == '\n' && !gSerialSkipLf)) {
                        if (C == '\r') {
                            gSerialSkipLf = 1;
                        }
                        FilesUiOnEnter();
                    } else if (C == '\n' && gSerialSkipLf) {
                        gSerialSkipLf = 0;
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
                    } else if (C == '\r' || (C == '\n' && !gSerialSkipLf)) {
                        if (C == '\r') {
                            gSerialSkipLf = 1;
                        }
                        EditUiOnEnter();
                    } else if (C == '\n' && gSerialSkipLf) {
                        gSerialSkipLf = 0;
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
                ConsoleSerialFeedChar(C, &gSerialSkipLf);
            }
        }
        /*
         * PR-G-shell-present：打字回显经 EchoMark 跳过逐键 Present，
         * 本处合并提交脏区（亦续传上次半途 gDirty）。virt/真机同路径。
         */
        /* 拖帧合成中后缓冲是半成品；这一刷会整块再贴，闪且卡 */
        if (!GuiPresentBlocked()) {
            HalVideoPresent();
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
