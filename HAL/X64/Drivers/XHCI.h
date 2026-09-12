/*
 * XHCI.h — USB 3.0 xHCI 主机控制器驱动接口
 *
 * 初始化控制器、枚举 USB 键盘、通过 MSI-X 中断接收 HID 报告。
 */
#ifndef XHCI_H
#define XHCI_H

#include "BootTypes.h"
#include "PCIe.h"

/* USB 控制传输 SETUP 阶段数据包 */
typedef struct {
    UINT8  bmRequestType;
    UINT8  bRequest;
    UINT16 wValue;
    UINT16 wIndex;
    UINT16 wLength;
} __attribute__((packed)) USB_SETUP_PACKET;

/* USB 设备描述符（部分字段） */
typedef struct {
    UINT8  bLength;
    UINT8  bDescriptorType;
    UINT16 bcdUSB;
    UINT8  bDeviceClass;
    UINT8  bDeviceSubClass;
    UINT8  bDeviceProtocol;
    UINT8  bMaxPacketSize0;
    UINT16 idVendor;
    UINT16 idProduct;
    UINT16 bcdDevice;
    UINT8  iManufacturer;
    UINT8  iProduct;
    UINT8  iSerialNumber;
    UINT8  bNumConfigurations;
} __attribute__((packed)) USB_DEVICE_DESCRIPTOR;

/* HID 键盘 8 字节引导协议报告 */
typedef struct {
    UINT8  ModifierKeys;
    UINT8  Reserved;
    UINT8  KeyCode[6];
} __attribute__((packed)) USB_KEYBOARD_REPORT;

/* USB 鼠标/平板报告；Absolute=1 时 X/Y 为 HID 0..32767 */
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
    INT8   Wheel;
    UINT8  Absolute;
} USB_MOUSE_REPORT;

/*
 * 真机输入模式（PR-H-xhci-base → dual → irq）
 * POLL：零 MSI，Drain 盲排空（产品默认 + dual 失败备份）
 * DUAL：MSI-X 已武装，但 Drain 仍盲排空作 backup；不通则 FallbackToPoll
 * IRQ ：纯中断（可选，dual 证明后再开）
 */
typedef enum {
    XHCI_IRQ_MODE_POLL = 0,
    XHCI_IRQ_MODE_DUAL = 1,
    XHCI_IRQ_MODE_IRQ = 2
} XHCI_IRQ_MODE;

int XhciInit(UINT64 BaseAddress);
int XhciHidKeyboardReady(void); /* 已 Address+Configure 键盘 */
/* 真机多 xHCI：当前控制器无 HID 时停 RS 并清 started，便于试下一颗 BAR */
void XhciAbandonNoHid(void);
int XhciEnableIrq(USB_CONTROLLER *Device);
/* PR-H-xhci-dual：试进 DUAL（MSI-X/MSI + Drain backup）；失败则 FallbackToPoll */
int XhciTryEnterDual(USB_CONTROLLER *Device);
/* PR-H-xhci-irq：仅当 gStatIrq 已证明投递后，Drain 内懒升为轻量 IRQ 模式 */
/* 真机：PHOTO 后再枚举鼠标，避免踩键盘 IN */
void XhciInitMouseDeferred(void);
/* PHOTO→桌面：清空鼠队列、重置累加坐标、Sync+Queue 键鼠 */
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
/* dual/irq 失败时切回 poll 备份 */
void XhciFallbackToPoll(const char *Why);
XHCI_IRQ_MODE XhciIrqMode(void);
void XhciDiagFormat(char *Buf, int Max); /* PHOTO/Shell：mode= + t/i/k/m/u/s/c/r/d/q */
void XhciDiagLogArms(void);              /* slot/DCI 期望值 */
void XhciIrq(void);
void XhciDrainEvents(void);
int XhciDequeueKeyboard(USB_KEYBOARD_REPORT *Report);
int XhciMousePresent(void);
int XhciDequeueMouse(USB_MOUSE_REPORT *Report);
int XhciUsesIrq(void);
int XhciKeyboardSetLeds(UINT8 Leds);

/*
 * PR-H-msc-2：Bulk/MSC API 壳。仅静态环 + 恒失败入口；
 * 不扫口、不 Reset、不 Address；不碰 Drain 键鼠热路径。
 * PR-H-msc-3：XhciMscScanPorts — 非键鼠口读 class 后放弃（无 Force PR/SetConfig/BOT）。
 * PR-H-msc-4：XhciMscClaimPorts — 单口 SetConfig + Bulk；不 SCSI。
 * PR-H-msc-5：XhciMscCapacity — BOT INQUIRY + READ CAPACITY(10)。
 */
int XhciMscBringUp(void); /* claim 后 0，否则 -1（仍会 Init Bulk 环） */
int XhciMscReady(void);   /* claim 后 1 */
/* DirIn=1 Bulk IN；已 claim 才可传 */
int XhciBulkXfer(int DirIn, void *Buf, UINT32 Len);
/* 返回打到 class 的口数；HC 未起 -1 */
int XhciMscScanPorts(void);
/* 返回 1 已 claim；0 无 MSC；-1 HC 未起 */
int XhciMscClaimPorts(void);
/* gMscScanSlot 已 Address：读配置 / SetConfig / Bulk；供根口与 hub 子口共用 */
int XhciMscFinishClaim(UINT32 RootPort, UINT8 Speed);
/* PR-H-msc-5：INQUIRY + READ CAPACITY；成功 0 */
int XhciMscCapacity(void);
UINT32 XhciMscBlockCount(void);
UINT32 XhciMscBlockSize(void);

#endif
