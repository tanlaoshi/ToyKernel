/*
 * XHCI.h — USB 3.0 xHCI 主机控制器驱动接口
 *
 * 初始化控制器、枚举 USB 键盘、通过 MSI-X 中断接收 HID 报告。
 */
#ifndef XHCI_H
#define XHCI_H

#include "BootConfig.h"
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

/* USB 鼠标/平板报告（绝对坐标或已换算的像素） */
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
} USB_MOUSE_REPORT;

int XhciInit(UINT64 BaseAddress);
int XhciEnableIrq(USB_CONTROLLER *Device);
void XhciIrq(void);
void XhciDrainEvents(void);
int XhciDequeueKeyboard(USB_KEYBOARD_REPORT *Report);
int XhciMousePresent(void);
int XhciDequeueMouse(USB_MOUSE_REPORT *Report);
int XhciUsesIrq(void);

#endif
