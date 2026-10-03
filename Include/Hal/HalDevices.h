/*
 * HalDevices.h — 块设备 / USB 输入 / 网卡 HAL 门面（Common 经 Hal.h 使用）
 */
#ifndef HAL_DEVICES_H
#define HAL_DEVICES_H

#include "BootTypes.h"

#define HAL_NET_IP_DEFAULT  0x0A00020FULL
#define HAL_IP_PROTO_ICMP   1
#define HAL_IP_PROTO_TCP    6
#define HAL_IP_PROTO_UDP    17

#define HAL_KBD_LED_NUM_LOCK     0x01
#define HAL_KBD_LED_CAPS_LOCK    0x02
#define HAL_KBD_LED_SCROLL_LOCK  0x04

typedef struct {
    UINT8 ModifierKeys;
    UINT8 Reserved;
    UINT8 KeyCode[6];
} HAL_KEYBOARD_REPORT;

/*
 * HAL_MOUSE_REPORT — 指针报告（PR-I1）
 *
 * Buttons：bit0=左、bit1=右、bit2=中（与 HID boot mouse 一致）。
 * Wheel：有符号滚轮步进（正=向上/远离用户，按 HID；无滚轮则为 0）。
 * Gui 消费滚轮见 PR-I2；本结构只负责 HAL→上层贯通。
 */
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
    INT8   Wheel;
    UINT8  Absolute; /* 1：X/Y 为 0..32767 平板坐标（QEMU usb-tablet） */
} HAL_MOUSE_REPORT;

int HalBlockInitialize(void);
/* PR-D2：注册平台驱动描述符（ATA / virtio-blk 等）；InitDriver 内调用 */
void HalDriverRegister(void);

int HalUsbInitialize(void);
/* PR-H-msc-2：MSC BringUp 壳；不自动认盘 */
int HalUsbMscInitialize(void);
int HalUsbMscReady(void);
/* PR-H-msc-3：Shell msc scan */
int HalUsbMscScan(void);
/* PR-H-msc-4：Shell msc claim */
int HalUsbMscClaim(void);
/* PR-H-msc-5：INQUIRY + READ CAPACITY */
int HalUsbMscCapacity(void);
UINT32 HalUsbMscBlockCount(void);
UINT32 HalUsbMscBlockSize(void);
/* PR-H-msc-6：装 BlockMux（不自动挂）；0 ok；负=错 */
int HalUsbMscMount(void);
/* PR-H-msc-7b：FS 前 auto；Live 默认开 */
int HalUsbMscAutoEnabled(void);
void HalUsbMscAutoSet(int On);
int HalUsbMscAutoBeforeFs(void);
/* PR-H-msc-hot */
int HalUsbMscRelease(void);
int HalUsbMscHotPoll(void);
int HalUsbMscHot(void);
/* PR-H-usb-uart-ftdi-1：xHCI 上认 FT232；非 x86 空操作 */
int HalUsbUartClaim(void);
int HalUsbUartReady(void);
/* PR-N-wifi-1：USB RTL8188EU；非 x86 空操作 */
int HalWifiClaim(void);
int HalWifiReady(void);
/* PR-N-wifi-1：NUC iwl8265；非 x86 空操作 */
int HalIwlClaim(void);
int HalIwlReady(void);
/* iwl 已关联+WPA2；托盘 Wi‑Fi 图标用此，勿用 Ready（仅 Probe） */
int HalIwlAssociated(void);
/* 刀 #114：后台关联泵；非 x86 空闲 */
int HalIwlBgBusy(void);
void HalIwlBgPump(void);
/* 上次 iwl 黄字时刻（x86 rdtsc）；0=还没有。供 ready 避开 rx=mic 夹提示符 */
UINT64 HalIwlLogTsc(void);
/* PR-G-igpu-1：核显 BAR 指纹；非 x86 空操作 */
void HalIgpuMmioInitialize(void);
/* PR-G-audio-1：HDA BAR 指纹；非 x86 空操作 */
void HalHdaMmioInitialize(void);
/* PR-G-audio-2：CORB/RIRB + codec 枚举；非 x86 空操作 */
void HalHdaCodecInitialize(void);
/* PR-G-audio-3：输出 Stream DMA；非 x86 空操作 */
void HalHdaStreamInitialize(void);
/* PR-G-audio-4：播 PCM；非 x86 恒失败；Samples=NULL→内置蜂鸣 */
int HalAudioProbe(void);
int HalAudioPlayPcm(const void *Samples, UINTN Bytes, UINT32 RateHz,
                    UINT32 Channels, UINT32 Bits);
void HalAudioStop(void);
void HalAudioBeep(void); /* PlayPcm(NULL) 快捷 */
/* PR-G-igpu-2：观察固件 GGTT/scanout；非 x86 空操作 */
void HalIgpuGttInitialize(void);
/* PR-G-igpu-3：forcewake + blit 骨架；非 x86 空操作 */
void HalIgpuForcewakeInitialize(void);
void HalIgpuBlitInitialize(void);
/* 可选自测：右上角 XY_COLOR_BLT（非 x86 空）；桌面不再自动调用（PR-G-igpu-corner） */
void HalIgpuBlitColorTest(void);
/* PR-G-igpu-4：大矩形 Present/CopyRect；非 x86 恒失败→CPU */
void HalIgpuPresentPrepare(void);
void HalIgpuPresentInvalidate(void);
int HalIgpuReady(void);
UINT32 HalIgpuBlitMinPixels(void);
void HalIgpuNotePresentSkipScale(void);
int HalIgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                       UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1);
int HalIgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                        UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                        UINT32 W, UINT32 H);
/* 真机：usb 模块末尾开 xHCI MSI-X；其它平台空操作 */
void HalInputArmIrq(void);
/* 真机：键鼠 ready 后再枚举鼠标，避免踩键盘 IN */
void HalInputInitializeMouseDeferred(void);
/* 进桌面前：清空鼠队列并对齐累加坐标 */
void HalInputMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
void HalInputPoll(void);
/* 诊断串：mode= + t/i/k/m…（Shell show xhci） */
void HalInputDiagFormat(char *Buf, int Max);
/* PR-H-ehci-1：EHCI CCS；无 EHCI 时 Buf="ready=0" */
void HalEhciDiagFormat(char *Buf, int Max);
/* PR-H-ehci-2：插上 USB 后再枚举 HID；0=ok */
int HalEhciHidRetry(void);
/* PR-H-ehci-4：ping FT232 tee；1=Bulk ok 0=未认 -1=Bulk 失败 */
int HalEhciFtdiPing(void);
/* PR-H-uhci-1：UHCI CCS 诊断串 */
void HalUhciDiagFormat(char *Buf, int Max);
/* PR-H-ps2-aux：kbd/aux/fail/pkts；retry 重开 Aux */
void HalPs2DiagFormat(char *Buf, int Max);
int HalPs2AuxRetry(void);
int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report);
int HalKeyboardSetLeds(UINT8 Leds);
int HalMousePresent(void);
int HalMouseDequeue(HAL_MOUSE_REPORT *Report);

int HalNetInitialize(void);
int HalNetReady(void);
/* 当前 L2 挂接次数；未挂过或本架构无此外置网卡时为 0 */
UINT32 HalNetNicEpoch(void);
void HalNetPoll(void);
void HalNetGetMacAddress(UINT8 Mac[6]);
UINT32 HalNetGetIpAddress(void);
void HalNetSetIpAddress(UINT32 Ip);
void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int HalNetParseIp(const char *Text, UINT32 *Ip);
int HalNetPing(const char *Host, int TimeoutMs);
void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
/* PR-H4e-2：有 e1000* 时返回 1 并填链路；virtio/无卡返回 0 */
int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex);
/* PR-N-nic-2slot：默认出站 0=有线 1=无线 -1=无外置 L2（virtio 等） */
int HalNetPrimaryKind(void);
/* PR-N-i219-note：Intel 网卡 PCI + e1000 Bind 只读现场；Write 通常=ConsoleWrite */
void HalNetDumpNicNote(void (*Write)(const char *Text));
int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 HalNetChecksum(const void *Data, UINTN Len);
void HalNetSetLwipReceive(int Enable);

/* 平台设备枚举：扫总线并对每个设备 DeviceAdd；由 DeviceEnumerateAll 调用 */
void HalDeviceEnumerate(void);
/* PR-DEV-tree-pci：扁平枚举后按 PCI 桥建父子边（最近桥 + Host 浅挂）。
 * x86 实现；arm64/riscv 无 PCI 不调用。只读配置空间。 */
void HalDeviceLinkPciTree(void);

#endif
