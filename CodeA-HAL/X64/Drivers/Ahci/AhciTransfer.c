/*
 * AhciTransfer.c — 扇区 DMA 传送（PR-S-ahci-1）
 */
#include "Ahci.h"
#include "AhciPrivate.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

int PortXfer(AHCI_DRIVE *Drive, UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    volatile AHCI_PORT *Port;
    AHCI_CMD_TABLE *Ct;
    UINT8 *Fis;
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 Done = 0;
    UINT64 BouncePhys;

    if (!Drive || !Drive->Ready || !Buffer || Count == 0) {
        return 0;
    }

    Port = Drive->Port;
    Ct = Drive->Ct;
    Fis = Ct->Fis;
    BouncePhys = (UINT64)(UINTN)Drive->Bounce;

    while (Done < Count) {
        UINT32 Chunk = Count - Done;
        UINT32 Bytes;
        UINT32 Cur = Lba + Done;
        UINT32 Tfd;
        UINT32 Is;
        int Spin;

        if (Chunk > AHCI_BOUNCE_SECTORS) {
            Chunk = AHCI_BOUNCE_SECTORS;
        }
        Bytes = Chunk * 512u;

        if (Write) {
            CopyMemory(Drive->Bounce, Data + (UINTN)Done * 512u, Bytes);
        }

        if (!WaitClear(&Port->Tfd, AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ, 1000000)) {
            return 0;
        }

        ZeroMemory(Fis, 64);
        Fis[0] = FIS_TYPE_REG_H2D;
        Fis[1] = 1u << 7; /* C=1 */
        Fis[2] = Write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
        Fis[3] = 0;
        Fis[4] = (UINT8)(Cur & 0xFF);
        Fis[5] = (UINT8)((Cur >> 8) & 0xFF);
        Fis[6] = (UINT8)((Cur >> 16) & 0xFF);
        Fis[7] = 0x40; /* LBA */
        Fis[8] = (UINT8)((Cur >> 24) & 0xFF);
        Fis[9] = 0;
        Fis[10] = 0;
        Fis[11] = 0;
        Fis[12] = (UINT8)(Chunk & 0xFF);
        Fis[13] = (UINT8)((Chunk >> 8) & 0xFF);

        ZeroMemory(&Ct->Prdt[0], sizeof(Ct->Prdt[0]));
        Ct->Prdt[0].Dba = (UINT32)BouncePhys;
        Ct->Prdt[0].Dbau = (UINT32)(BouncePhys >> 32);
        Ct->Prdt[0].Dbc = (Bytes - 1u) | (1u << 31);

        Drive->Cl[0].Flags = (UINT16)(5u | (Write ? (1u << 6) : 0));
        Drive->Cl[0].Prdtl = 1;
        Drive->Cl[0].Prdbc = 0;

        MmioWrite32(&Port->Is, 0xFFFFFFFFu);
        Fence();
        MmioWrite32(&Port->Ci, 1u);

        Spin = 2000000;
        while (Spin-- > 0) {
            UINT32 Ci = MmioRead32(&Port->Ci);
            Is = MmioRead32(&Port->Is);
            if ((Ci & 1u) == 0) {
                break;
            }
            if (Is & AHCI_PxIS_TFES) {
                return 0;
            }
            HalCpuRelax();
        }
        if (Spin <= 0) {
            return 0;
        }

        Is = MmioRead32(&Port->Is);
        Tfd = MmioRead32(&Port->Tfd);
        if ((Is & AHCI_PxIS_TFES) || (Tfd & AHCI_PxTFD_ERR)) {
            return 0;
        }
        MmioWrite32(&Port->Is, 0xFFFFFFFFu);

        if (!Write) {
            CopyMemory(Data + (UINTN)Done * 512u, Drive->Bounce, Bytes);
        }
        Done += Chunk;
    }
    return 1;
}
