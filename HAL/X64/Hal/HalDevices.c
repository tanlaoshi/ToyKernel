/*
 * HalDevices.c — x86：Input / Net 经 Driver 类门面（PR-D3）
 *
 * Common 只见 HalInput* / HalNet*；本文件不 include XHCI/Net 私有实现细节以外的
 * 注册入口头（仍经 Drivers/ 薄包装注册）。
 */
#include "Hal.h"
#include "DriverInput.h"
#include "DriverNet.h"
#include "InputXhci.h"
#include "InputPs2.h"
#include "Net.h"
#include "UsbMsc.h"
#include "E1000.h"
#include "XHCI.h"

#ifndef TOY_DEMO_DRIVER
#define TOY_DEMO_DRIVER 1
#endif

/* BlockAta / BlockAhci（H1）/ BlockNvme（H5） */
void AtaDriverRegister(void);
void AhciDriverRegister(void);
void NvmeDriverRegister(void);
void MscDriverRegister(void); /* PR-H-msc：空壳注册；认盘在后续 PR */
void E1000DriverRegister(void);
void DemoDriverRegister(void); /* PR-D-tpl-2 */
void XhciDiagFormat(char *Buf, int Max);
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);

void HalDriverRegister(void) {
    /* 后注册者在 HalBlockInit 再 Probe 时可覆盖后端：NVMe > AHCI > ATA */
    AhciDriverRegister();
    AtaDriverRegister();
    NvmeDriverRegister();
    MscDriverRegister(); /* PR-H-msc-1：Bind 不改 Block 后端 */
    InputXhciRegister(); /* 先 USB HID */
    InputPs2Register();  /* 后 PS/2：仅当 xhci-hid 未绑 Input 时生效 */
    NetDriverRegister();
    E1000DriverRegister(); /* PR-H4：无卡 Probe 失败；有卡时可覆盖 virtio */
#if TOY_DEMO_DRIVER
    DemoDriverRegister(); /* PR-D-tpl-2：课堂 Demo；-DTOY_DEMO_DRIVER=0 可关 */
#endif
}

int HalUsbInit(void) {
    return InputXhciInit();
}

int HalUsbMscInit(void) {
    return UsbMscInit();
}

int HalUsbMscReady(void) {
    return UsbMscReady();
}

int HalUsbMscScan(void) {
    return UsbMscScan();
}

int HalUsbMscClaim(void) {
    return UsbMscClaim();
}

int HalUsbMscCapacity(void) {
    return UsbMscCapacity();
}

UINT32 HalUsbMscBlockCount(void) {
    return UsbMscBlockCount();
}

UINT32 HalUsbMscBlockSize(void) {
    return UsbMscBlockSize();
}

int HalUsbMscMount(void) {
    return UsbMscMount();
}

int HalUsbMscAutoEnabled(void) {
    return UsbMscAutoEnabled();
}

void HalUsbMscAutoSet(int On) {
    UsbMscAutoSet(On);
}

int HalUsbMscAutoBeforeFs(void) {
    return UsbMscAutoBeforeFs();
}

int HalUsbMscRelease(void) {
    return UsbMscRelease();
}

int HalUsbMscHotPoll(void) {
    return UsbMscHotPoll();
}

int HalUsbMscHot(void) {
    return UsbMscHot();
}

int HalUsbUartClaim(void) {
    int Rc;

    /* FTDI 优先；已认则 CDC 互斥跳过 */
    Rc = XhciFtdiClaim();
    if (Rc == 1) {
        return 1;
    }
    return XhciCdcClaim();
}

int HalUsbUartReady(void) {
    return (XhciFtdiReady() || XhciCdcReady()) ? 1 : 0;
}

void HalInputArmIrq(void) {
    InputXhciArmIrq();
}

void HalInputInitMouseDeferred(void) {
    /* 已恢复枚举期绑鼠标（ea8a865）；保留符号以免旧调用方链接失败 */
}

void HalInputMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    XhciMouseHandoffDesktop(CursorX, CursorY);
}

void HalInputPoll(void) {
    ToyDriverInputPoll();
}

void HalInputDiagFormat(char *Buf, int Max) {
    XhciDiagFormat(Buf, Max);
}

int HalKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    return ToyDriverInputKeyboardDequeue(Report);
}

int HalKeyboardSetLeds(UINT8 Leds) {
    return ToyDriverInputKeyboardSetLeds(Leds);
}

int HalMousePresent(void) {
    return ToyDriverInputMousePresent();
}

int HalMouseDequeue(HAL_MOUSE_REPORT *Report) {
    return ToyDriverInputMouseDequeue(Report);
}

int HalNetInit(void) {
    return NetInit();
}

int HalNetReady(void) {
    return ToyDriverNetReady();
}

void HalNetPoll(void) {
    ToyDriverNetPoll();
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
    ToyDriverNetGetMac(Mac);
}

UINT32 HalNetGetIpAddress(void) {
    return ToyDriverNetGetIp();
}

void HalNetSetIpAddress(UINT32 Ip) {
    ToyDriverNetSetIp(Ip);
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    ToyDriverNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return ToyDriverNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return ToyDriverNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    ToyDriverNetGetStats(TxDone, RxFrames);
}

/* PR-N-nic：链路走已挂 NIC_L2（e1000 等）；virtio / 无 L2 → 0 */
int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (NetNicGetLink(Up, Mbps, FullDuplex) != 0) {
        return 0;
    }
    return 1;
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    E1000DumpNote(Write);
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return ToyDriverNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return ToyDriverNetChecksum(Data, Len);
}

void HalNetSetLwipReceive(int Enable) {
    ToyDriverNetSetLwIpRx(Enable);
}
