/*
 * VideoPresentRect.c — Present 条带 blit（PR-S3-videopresent-1）
 */
#include "VideoPrivate.h"
#include "HalDevices.h"
#include "HalSerial.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

/* 将脏区 blit 到 GOP；无后缓冲时为空操作。scale=100 且逻辑=物理时 1:1 memcpy */
void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
                     UINT64 FbBytes, int *DirtyOut, UINT32 *Dx0,
                     UINT32 *Dy0, UINT32 *Dx1, UINT32 *Dy1,
                     int *OutPartial) {
    UINT32 Y;
    UINT32 ChunkEnd;
    UINT64 RowOff;
    UINT64 RowBytes;
    UINT64 Flags;
    int Scaled = (gUiScale != 100u) || (gScreen.Width != gPhysW) ||
                 (gScreen.Height != gPhysH);
    UINT32 ChunkRows = gPresentChunkRows;

    if (ChunkRows == 0) {
        ChunkRows = PRESENT_CHUNK_ROWS;
    }

    *OutPartial = 0;
    if (X0 >= X1 || Y0 >= Y1) {
        return;
    }

    if (Scaled) {
        HalIgpuNotePresentSkipScale();
    }

    if (!Scaled) {
        RowBytes = (UINT64)(X1 - X0) * 4ull;
        /*
         * igpu-4：大矩形且 blit 就绪时一次 SRC_COPY；失败仍走下方 memcpy。
         * 缩放路径保持 CPU（最近邻）。
         */
        if (gBack && gBackOn && HalIgpuReady()
            && ((UINT64)(X1 - X0) * (UINT64)(Y1 - Y0)
                >= (UINT64)HalIgpuBlitMinPixels())
            && HalIgpuPresentRect(gBack, gBackPitch, gFrontPitch,
                               gPhysH ? gPhysH : gScreen.Height,
                               X0, Y0, X1, Y1)) {
            return;
        }
        Y = Y0;
        while (Y < Y1) {
            /* 勿 Y+ChunkRows：ChunkRows=0xFFFFFFFF 会回绕 → Y 不前进死循环 */
            if (ChunkRows >= (Y1 - Y)) {
                ChunkEnd = Y1;
            } else {
                ChunkEnd = Y + ChunkRows;
            }
            Flags = HalIrqSave();
            for (; Y < ChunkEnd; Y++) {
                const UINT32 *Src;
                UINT32 *Dst;

                RowOff = ((UINT64)Y * (UINT64)gFrontPitch + (UINT64)X0) * 4ull;
                if (RowOff + RowBytes > FbBytes) {
                    *Dx0 = X0;
                    *Dy0 = Y;
                    *Dx1 = X1;
                    *Dy1 = Y1;
                    *DirtyOut = 1;
                    *OutPartial = 1;
                    HalIrqRestore(Flags);
                    return;
                }
                Src = &gBack[Y * gBackPitch + X0];
                Dst = &gFront[Y * gFrontPitch + X0];
                memcpy(Dst, Src, (UINTN)RowBytes);
            }
            HalIrqRestore(Flags);
            /* 条带间抽 COM1→软环（IRQ 未开时）；勿 SchedulerIoBreath——会 GuiPoll→Present 重入 */
            if (HalSerialPresent()) {
                HalSerialRxPump();
            }
        }
        return;
    }

    /* 最近邻：逻辑脏区 → 物理矩形；按物理行条带 cli */
    {
        UINT32 Px0;
        UINT32 Py0;
        UINT32 Px1;
        UINT32 Py1;
        UINT32 Py;
        UINT32 ChunkPy;

        if (gPhysW == 0 || gPhysH == 0 || gScreen.Width == 0 ||
            gScreen.Height == 0) {
            return;
        }
        /*
         * 上取整 + 外扩 8 物理像素：150%/200% 最近邻时否则光标/图标拖尾。
         */
        Px0 = (UINT32)(((UINT64)X0 * (UINT64)gPhysW) / (UINT64)gScreen.Width);
        Py0 = (UINT32)(((UINT64)Y0 * (UINT64)gPhysH) / (UINT64)gScreen.Height);
        Px1 = (UINT32)((((UINT64)X1 * (UINT64)gPhysW) + (UINT64)gScreen.Width - 1u) /
                       (UINT64)gScreen.Width);
        Py1 = (UINT32)((((UINT64)Y1 * (UINT64)gPhysH) + (UINT64)gScreen.Height - 1u) /
                       (UINT64)gScreen.Height);
        if (Px0 >= 8u) {
            Px0 -= 8u;
        } else {
            Px0 = 0;
        }
        if (Py0 >= 8u) {
            Py0 -= 8u;
        } else {
            Py0 = 0;
        }
        if (Px1 + 8u < gPhysW) {
            Px1 += 8u;
        } else {
            Px1 = gPhysW;
        }
        if (Py1 + 8u < gPhysH) {
            Py1 += 8u;
        } else {
            Py1 = gPhysH;
        }
        if (Px0 >= Px1 || Py0 >= Py1) {
            return;
        }
        Py = Py0;
        while (Py < Py1) {
            if (ChunkRows >= (Py1 - Py)) {
                ChunkPy = Py1;
            } else {
                ChunkPy = Py + ChunkRows;
            }
            Flags = HalIrqSave();
            for (; Py < ChunkPy; Py++) {
                UINT32 Ly;
                UINT32 Px;
                UINT32 *Dst;

                Ly = (UINT32)(((UINT64)Py * (UINT64)gScreen.Height) /
                              (UINT64)gPhysH);
                if (Ly >= gScreen.Height) {
                    Ly = gScreen.Height - 1;
                }
                RowOff = ((UINT64)Py * (UINT64)gFrontPitch + (UINT64)Px0) * 4ull;
                if (RowOff + (UINT64)(Px1 - Px0) * 4ull > FbBytes) {
                    /* 映射回逻辑脏区续传 */
                    *Dx0 = X0;
                    *Dy0 = (UINT32)(((UINT64)Py * (UINT64)gScreen.Height) /
                                    (UINT64)gPhysH);
                    *Dx1 = X1;
                    *Dy1 = Y1;
                    *DirtyOut = 1;
                    *OutPartial = 1;
                    HalIrqRestore(Flags);
                    return;
                }
                Dst = &gFront[Py * gFrontPitch + Px0];
                for (Px = Px0; Px < Px1; Px++) {
                    UINT32 Lx = (UINT32)(((UINT64)Px * (UINT64)gScreen.Width) /
                                         (UINT64)gPhysW);
                    if (Lx >= gScreen.Width) {
                        Lx = gScreen.Width - 1;
                    }
                    Dst[Px - Px0] = gBack[Ly * gBackPitch + Lx];
                }
            }
            HalIrqRestore(Flags);
            if (HalSerialPresent()) {
                HalSerialRxPump();
            }
        }
    }
}
