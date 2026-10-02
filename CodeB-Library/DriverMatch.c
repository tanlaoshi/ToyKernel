/*
 * DriverMatch.c — 驱动匹配表过滤（PR-DRV-match-logic）
 *
 * DriverMatchesDevice：按 DRIVER_MATCH 表过滤 DEVICE_NODE。
 * Match==NULL 由 ToyDriverProbeClass 走旧路径（Probe 自扫），本文件只管有表路径。
 * 规格见 Documents/待做/设备管理器驱动匹配.md。
 */
#include "Driver.h"
#include "Device.h"

static int StrEq(const char *A, const char *B) {
    while (*A && *B) {
        if (*A != *B) return 0;
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

/*
 * 命中规则（§3.4）：
 *   PCI ：(Vendor & VendorMask)==(entry.Vendor & VendorMask) 且
 *         (Device & DeviceMask)==(entry.Device & DeviceMask)；Mask=0 即通配。
 *   DTB ：Compatible 串全等。
 *   FIXED：Bar[0]==entry.Base。
 * 表以 DRIVER_MATCH_NONE 结尾；任一条命中即返回 1。
 */
int DriverMatchesDevice(const TOY_DRIVER *Drv, const DEVICE_NODE *Dev) {
    const DRIVER_MATCH *M;

    if (!Drv || !Dev || !Drv->Match) {
        return 0;
    }
    for (M = Drv->Match; M->Type != DRIVER_MATCH_NONE; M++) {
        if (M->Type == DRIVER_MATCH_PCI) {
            if (Dev->Bus != DEVICE_BUS_PCI) continue;
            if ((Dev->Vendor & M->U.Pci.VendorMask) !=
                (M->U.Pci.Vendor & M->U.Pci.VendorMask)) continue;
            if ((Dev->Device & M->U.Pci.DeviceMask) !=
                (M->U.Pci.Device & M->U.Pci.DeviceMask)) continue;
            return 1;
        } else if (M->Type == DRIVER_MATCH_DTB) {
            if (Dev->Bus != DEVICE_BUS_DTB) continue;
            if (StrEq(Dev->Compatible, M->U.Dtb.Compatible)) return 1;
        } else if (M->Type == DRIVER_MATCH_FIXED) {
            if (Dev->Bus != DEVICE_BUS_FIXED) continue;
            if (Dev->Bar[0] == M->U.Fixed.Base) return 1;
        }
    }
    return 0;
}
