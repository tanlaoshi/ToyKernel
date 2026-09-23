/*
 * DeviceEnum.c — x86-64 设备枚举：只读扫 PCI（PR-DEV-2）
 *
 * 禁止批量写 Command；BAR 对齐 PciScanUSBControllers 的 64-bit 逻辑。
 */
#include "Device.h"
#include "PCIe.h"
#include "PciNames.h"
#include "Hal.h"
#include "Debug.h"

static const char *PciClassName(UINT8 Class, UINT8 Subclass, UINT8 ProgIf) {
    if (Class == 0x01) {
        if (Subclass == 0x06) {
            return "ahci";
        }
        if (Subclass == 0x08) {
            return "nvme";
        }
        return "storage";
    }
    if (Class == 0x02) {
        return "net";
    }
    if (Class == 0x03) {
        return "display";
    }
    if (Class == 0x04) {
        return "audio";
    }
    if (Class == 0x05) {
        return "mem";
    }
    if (Class == 0x06) {
        return "bridge";
    }
    if (Class == 0x07) {
        return "serial";
    }
    if (Class == 0x08) {
        return "periph";
    }
    if (Class == 0x09) {
        return "input";
    }
    if (Class == 0x0C) {
        if (Subclass == 0x03) {
            if (ProgIf == 0x30) {
                return "xhci";
            }
            if (ProgIf == 0x20) {
                return "ehci";
            }
            return "usb";
        }
        if (Subclass == 0x05) {
            return "smbus";
        }
        return "serialbus";
    }
    return "misc";
}

static void ZeroBytes(void *Ptr, UINTN Bytes) {
    UINT8 *P = (UINT8 *)Ptr;
    UINTN i;

    for (i = 0; i < Bytes; i++) {
        P[i] = 0;
    }
}

static void CopyName(char *Dst, UINTN Max, const char *Src) {
    UINTN i;

    if (!Dst || Max == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Max && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static void FillBars(UINT8 Bus, UINT8 Dev, UINT8 Func, UINT64 OutBar[6]) {
    UINT32 RawBar[6];
    int B;

    for (B = 0; B < 6; B++) {
        RawBar[B] = PciReadConfig(Bus, Dev, Func, (UINT8)(0x10 + B * 4));
        OutBar[B] = 0;
    }
    for (B = 0; B < 6; ) {
        if (RawBar[B] & 1) {
            /* IO BAR：低位清 type；教学用保留端口基址 */
            OutBar[B] = (UINT64)(RawBar[B] & 0xFFFFFFFCu);
            B++;
            continue;
        }
        if ((RawBar[B] & 6) == 4 && B + 1 < 6) {
            OutBar[B] = ((UINT64)RawBar[B + 1] << 32) | (RawBar[B] & 0xFFFFFFF0u);
            B += 2;
        } else {
            OutBar[B] = (UINT64)(RawBar[B] & 0xFFFFFFF0u);
            B++;
        }
    }
}

void HalDeviceEnumerate(void) {
    int Bus;
    int Dev;
    int Func;
    int Added = 0;

    for (Bus = 0; Bus < 256; Bus++) {
        for (Dev = 0; Dev < 32; Dev++) {
            for (Func = 0; Func < 8; Func++) {
                UINT32 VidDid;
                UINT16 Vendor;
                UINT16 DeviceId;
                UINT32 ClassCode;
                UINT8 Class;
                UINT8 Subclass;
                UINT8 ProgIf;
                UINT32 IrqDw;
                DEVICE_NODE Node;
                const char *Name;
                const char *Friendly;

                VidDid = PciReadConfig((UINT8)Bus, (UINT8)Dev, (UINT8)Func, 0x00);
                Vendor = (UINT16)(VidDid & 0xFFFFu);
                DeviceId = (UINT16)((VidDid >> 16) & 0xFFFFu);
                if (Vendor == 0xFFFFu) {
                    if (Func == 0) {
                        break;
                    }
                    continue;
                }

                ClassCode = PciReadConfig((UINT8)Bus, (UINT8)Dev, (UINT8)Func, 0x08);
                Class = (UINT8)((ClassCode >> 24) & 0xFFu);
                Subclass = (UINT8)((ClassCode >> 16) & 0xFFu);
                ProgIf = (UINT8)((ClassCode >> 8) & 0xFFu);
                IrqDw = PciReadConfig((UINT8)Bus, (UINT8)Dev, (UINT8)Func, 0x3C);

                ZeroBytes(&Node, sizeof(Node));
                Node.Bus = DEVICE_BUS_PCI;
                Node.Vendor = Vendor;
                Node.Device = DeviceId;
                Node.PciBus = (UINT8)Bus;
                Node.PciDev = (UINT8)Dev;
                Node.PciFn = (UINT8)Func;
                Node.Irq = (UINT8)(IrqDw & 0xFFu);
                FillBars((UINT8)Bus, (UINT8)Dev, (UINT8)Func, Node.Bar);

                Name = PciClassName(Class, Subclass, ProgIf);
                CopyName(Node.Name, sizeof(Node.Name), Name);
                /* PR-DEV-enum：类码入库；友好名 设备表 → 类名 → 短键。不写 Command、不探 BAR size */
                Node.Class = Class;
                Node.Subclass = Subclass;
                Node.ProgIf = ProgIf;
                Friendly = PciGetDeviceName(Vendor, DeviceId);
                if (!Friendly) {
                    Friendly = PciGetClassName(Class, Subclass, ProgIf);
                }
                if (!Friendly) {
                    Friendly = Name;
                }
                CopyName(Node.FriendlyName, sizeof(Node.FriendlyName), Friendly);

                if (DeviceAdd(&Node) >= 0) {
                    Added++;
                }

                /* 单功能：Func0 的 HeaderType bit7=0 则不再扫同槽其它 Func */
                if (Func == 0) {
                    UINT32 Ht = PciReadConfig((UINT8)Bus, (UINT8)Dev, 0, 0x0C);
                    if (((Ht >> 16) & 0x80u) == 0) {
                        break;
                    }
                }
            }
        }
    }

    DebugWrite("DeviceEnum: found ");
    DebugHex32((UINT32)Added);
    DebugWrite(" PCI devices\n");
}
