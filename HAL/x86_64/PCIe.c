/*
 * PCIe.c — PCI 配置空间访问与 USB 控制器枚举
 *
 * 扫描所有 Bus/Dev/Fn，识别 Class 0x0C/Sub 0x03 的 USB 控制器，
 * 解析 BAR 并启用 Bus Master + IO + Memory。支持 MSI-X 与 MSI 中断。
 */
#include "PCIe.h"
#include "Console.h"
#include "Debug.h"
#include "hal.h"

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* 读取 PCI 配置空间 32 位寄存器（Offset 须 4 字节对齐） */
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

#if TOY_DEBUG
                    {
                        char *TypeStr;
                        if (ProgIF == 0x00) TypeStr = "UHCI";
                        else if (ProgIF == 0x10) TypeStr = "OHCI";
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

/* 在配置空间能力链中查找指定 Cap ID，返回偏移或 0 */
static int PciFindCap(UINT8 Bus, UINT8 Device, UINT8 Function, UINT8 Id) {
    UINT32 Status = PciReadConfig(Bus, Device, Function, 0x04);
    if (!((Status >> 16) & 0x10)) {
        return 0;
    }
    UINT32 Ptr = PciReadConfig(Bus, Device, Function, 0x34) & 0xFC;
    int Guard = 0;
    while (Ptr && Guard++ < 48) {
        UINT32 Val = PciReadConfig(Bus, Device, Function, (UINT8)Ptr);
        if ((Val & 0xFF) == Id) {
            return (int)Ptr;
        }
        Ptr = (Val >> 8) & 0xFC;
    }
    return 0;
}

/* 为 USB 设备配置 MSI-X（优先）或 MSI，绑定到 LAPIC 向量 Vector */
int PciEnableMsi(USB_CONTROLLER *Device, UINT8 Vector) {
    UINT32 Cmd = PciReadConfig(Device->Bus, Device->Device, Device->Function, 0x04);
    Cmd |= (1u << 10);
    PciWriteConfig(Device->Bus, Device->Device, Device->Function, 0x04, Cmd);

    int Cap = PciFindCap(Device->Bus, Device->Device, Device->Function, 0x11);
    if (Cap) {
        UINT32 TableDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4));
        UINT32 Bir = TableDw & 7;
        UINT32 Off = TableDw & 0xFFFFFFF8u;
        if (Bir > 5 || Device->Bar[Bir] == 0) {
            DebugWrite("MSI-X: bad BIR\n");
            return 0;
        }
        UINT64 Entry = Device->Bar[Bir] + Off;
        *(volatile UINT32 *)(UINTN)(Entry + 12) = 1;
        *(volatile UINT32 *)(UINTN)(Entry + 0) = 0xFEE00000u;
        *(volatile UINT32 *)(UINTN)(Entry + 4) = 0;
        *(volatile UINT32 *)(UINTN)(Entry + 8) = Vector;
        __asm__ volatile ("mfence" ::: "memory");
        *(volatile UINT32 *)(UINTN)(Entry + 12) = 0;
        __asm__ volatile ("mfence" ::: "memory");

        UINT32 Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
        Dw0 |= (1u << 31);
        Dw0 &= ~(1u << 30);
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap, Dw0);
        DebugWrite("MSI-X enabled vec=");
        DebugHex32(Vector);
        DebugWrite("\n");
        return 1;
    }

    Cap = PciFindCap(Device->Bus, Device->Device, Device->Function, 0x05);
    if (!Cap) {
        DebugWrite("PCI: no MSI/MSI-X\n");
        return 0;
    }
    UINT32 Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
    UINT16 Ctl = (UINT16)(Dw0 >> 16);
    if (Ctl & (1u << 7)) {
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4), 0xFEE00000u);
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8), 0);
        UINT32 DataDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 12));
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 12),
                       (DataDw & 0xFFFF0000u) | Vector);
    } else {
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 4), 0xFEE00000u);
        UINT32 DataDw = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8));
        PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)(Cap + 8),
                       (DataDw & 0xFFFF0000u) | Vector);
    }
    Ctl |= 1;
    Dw0 = PciReadConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap);
    PciWriteConfig(Device->Bus, Device->Device, Device->Function, (UINT8)Cap,
                   (Dw0 & 0xFFFFu) | ((UINT32)Ctl << 16));
    DebugWrite("MSI enabled vec=");
    DebugHex32(Vector);
    DebugWrite("\n");
    return 1;
}