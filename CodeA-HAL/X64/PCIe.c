/*
 * PCIe.c — PCI 配置空间与 USB 控制器枚举（PR-S3-pcie-1）
 *
 * MSI/MSI-X / IOAPIC INTx 见 PCIeMsi.c。
 */
#include "PCIe.h"
#include "Console.h"
#include "Debug.h"
#include "Hal.h"

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

UINT32 PciReadConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    HalIoWrite32(PCI_CONFIG_ADDRESS, Address);
    return HalIoRead32(PCI_CONFIG_DATA);
}

/* 写入 PCI 配置空间 32 位寄存器 */
void PciWriteConfig(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Offset, UINT32 Value) {
    UINT32 Address = (1 << 31) | (Bus << 16) | (Device << 11) | (Function << 8) | (Offset & 0xFC);
    HalIoWrite32(PCI_CONFIG_ADDRESS, Address);
    HalIoWrite32(PCI_CONFIG_DATA, Value);
}

/* 将 UINT32 格式化为静态缓冲区中的 0x 十六进制字符串 */
char* Uint32ToHex(UINT32 Value) {
    static char Buf[16];
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        int Digit = (Value >> (i * 4)) & 0xF;
        Buf[9 - i] = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
    }
    Buf[10] = '\0';
    return Buf;
}

/* 将 UINT8 格式化为十进制字符串 */
char* Uint8ToDecimal(UINT8 Value) {
    static char Buf[8];
    if (Value >= 100) {
        Buf[0] = '0' + Value / 100;
        Buf[1] = '0' + (Value % 100) / 10;
        Buf[2] = '0' + Value % 10;
        Buf[3] = '\0';
    } else if (Value >= 10) {
        Buf[0] = '0' + Value / 10;
        Buf[1] = '0' + Value % 10;
        Buf[2] = '\0';
    } else {
        Buf[0] = '0' + Value;
        Buf[1] = '\0';
    }
    return Buf;
}

/* 将 UINT64 格式化为 0x 十六进制字符串 */
char* Uint64ToHex(UINT64 Value) {
    static char Buf[20];
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        int Digit = (Value >> (i * 4)) & 0xF;
        Buf[17 - i] = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
    }
    Buf[18] = '\0';
    return Buf;
}

/* 扫描 PCI 总线，将 USB 控制器填入 Controllers 数组，返回找到的数量 */
int PciScanUSBControllers(USB_CONTROLLER *Controllers, int MaxControllers) {
    int Count = 0;
    int Found = 0;
    
    DebugWrite("PCI Scanning...\n");
    
    for (int Bus = 0; Bus < 256; Bus++) {
        for (int Device = 0; Device < 32; Device++) {
            for (int Function = 0; Function < 8; Function++) {
                UINT32 VendorDevice = PciReadConfig(Bus, Device, Function, 0x00);
                UINT16 VendorID = VendorDevice & 0xFFFF;
                
                if (VendorID == 0xFFFF) continue;
                
                UINT32 ClassCode = PciReadConfig(Bus, Device, Function, 0x08);
                UINT8 Class = (ClassCode >> 24) & 0xFF;
                UINT8 Subclass = (ClassCode >> 16) & 0xFF;
                UINT8 ProgIF = (ClassCode >> 8) & 0xFF;
                
                if (Class == 0x0C && Subclass == 0x03) {
                    if (Count >= MaxControllers) return Count;

                    UINT32 Command = PciReadConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, 0x04);
                    Command |= 0x07;
                    PciWriteConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, 0x04, Command);

                    UINT32 RawBar[6];
                    for (int B = 0; B < 6; B++) {
                        RawBar[B] = PciReadConfig((UINT8)Bus, (UINT8)Device, (UINT8)Function, (UINT8)(0x10 + B * 4));
                    }
                    for (int B = 0; B < 6; B++) {
                        Controllers[Count].Bar[B] = 0;
                    }

                    /*
                     * UHCI（ProgIF 00）：寄存器在 I/O BAR（常 BAR4），非 MMIO。
                     * 旧逻辑跳过 bit0=1 的 BAR → UHCI 永远扫不到（PR-H-uhci-1）。
                     */
                    if (ProgIF == 0x00) {
                        UINT16 IoBase = 0;
                        int Bi;
                        for (Bi = 0; Bi < 6; Bi++) {
                            if (RawBar[Bi] & 1u) {
                                IoBase = (UINT16)(RawBar[Bi] & 0xFFFCu);
                                Controllers[Count].Bar[Bi] = IoBase;
                                break;
                            }
                        }
                        if (IoBase == 0) {
                            continue;
                        }
                        Controllers[Count].Bus = (UINT8)Bus;
                        Controllers[Count].Device = (UINT8)Device;
                        Controllers[Count].Function = (UINT8)Function;
                        Controllers[Count].BaseAddress = IoBase;
                        Controllers[Count].Type = ProgIF;
#if TOY_KERNEL_DEBUG
                        DebugWrite("USB: UHCI io=");
                        DebugWrite(Uint64ToHex(IoBase));
                        DebugWrite("\n");
#endif
                        Count++;
                        Found = 1;
                        continue;
                    }

                    for (int B = 0; B < 6; ) {
                        if (RawBar[B] & 1) {
                            B++;
                            continue;
                        }
                        if ((RawBar[B] & 6) == 4 && B + 1 < 6) {
                            Controllers[Count].Bar[B] =
                                ((UINT64)RawBar[B + 1] << 32) | (RawBar[B] & 0xFFFFFFF0);
                            B += 2;
                        } else {
                            Controllers[Count].Bar[B] = RawBar[B] & 0xFFFFFFF0;
                            B++;
                        }
                    }

                    UINT64 BaseAddress = Controllers[Count].Bar[0];
                    if (BaseAddress == 0) {
                        continue;
                    }

                    Controllers[Count].Bus = (UINT8)Bus;
                    Controllers[Count].Device = (UINT8)Device;
                    Controllers[Count].Function = (UINT8)Function;
                    Controllers[Count].BaseAddress = BaseAddress;
                    Controllers[Count].Type = ProgIF;

#if TOY_KERNEL_DEBUG
                    {
                        char *TypeStr;
                        if (ProgIF == 0x10) TypeStr = "OHCI";
                        else if (ProgIF == 0x20) TypeStr = "EHCI";
                        else if (ProgIF == 0x30) TypeStr = "XHCI";
                        else TypeStr = "USB";
                        DebugWrite("USB: ");
                        DebugWrite(TypeStr);
                        DebugWrite(" at ");
                        DebugWrite(Uint64ToHex(BaseAddress));
                        DebugWrite("\n");
                    }
#endif
                    
                    Count++;
                    Found = 1;
                }
            }
        }
    }
    
    if (!Found) {
        DebugWrite("No USB Controller found!\n");
    } else {
        DebugWrite("PCI Scan Complete\n");
    }
    
    return Count;
}

/* 在配置空间能力链中查找指定 Cap ID，返回偏移或 0（PR-DEV-irq-mode：导出供枚举记录能力） */
