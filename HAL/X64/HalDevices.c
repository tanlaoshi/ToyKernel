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
#include "BlockMux.h"
#include "Block.h"

/* BlockAta / BlockAhci（H1）/ BlockNvme（H5） */
void AtaDriverRegister(void);
void AhciDriverRegister(void);
void NvmeDriverRegister(void);
void MscDriverRegister(void); /* PR-H-msc：Bind 不自动 Mux */
const BLOCK_BACKEND *BlockMscBackend(void);
void E1000DriverRegister(void);
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

/*
 * PR-H-msc-6：已 claim 后安装 Mux 并重 Probe。
 * 成功 0；-1 未 claim；-2 capacity/bsize；-3 无盘。
 * 调用方再 FileSystemRemountVolumes（不自动挂）。
 */
int HalUsbMscMount(void) {
    int N;

    if (!UsbMscReady()) {
        return -1;
    }
    if (UsbMscBlockCount() == 0) {
        if (UsbMscCapacity() != 0) {
            return -2;
        }
    }
    if (UsbMscBlockSize() != 512u) {
        return -2;
    }
    BlockMuxInstallMsc(BlockMscBackend());
    N = HalBlockInit();
    if (N <= 0) {
        return -3;
    }
    return 0;
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

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return ToyDriverNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return ToyDriverNetChecksum(Data, Len);
}

void HalNetSetLwipReceive(int Enable) {
    ToyDriverNetSetLwIpRx(Enable);
}
