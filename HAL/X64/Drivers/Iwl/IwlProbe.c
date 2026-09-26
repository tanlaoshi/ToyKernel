/*
 * IwlProbe.c — PCI 8086:24fd 查找（PR-N-wifi-1）
 *
 * 本刀只认卡；不 Map BAR、不碰 MMIO（wifi-2 灌固件时再开）。
 */
#include "IwlPrivate.h"
#include "PCIe.h"

int IwlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT16 *DidOut) {
    int B;
    int D;
    int F;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 Cmd;

                if (Vid != IWL_VENDOR || Did != IWL_DID_8265) {
                    continue;
                }
                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                if (DidOut) {
                    *DidOut = Did;
                }
                return 1;
            }
        }
    }
    return 0;
}
