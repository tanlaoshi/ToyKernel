/*
 * VideoPresent.c — 脏矩形 / 光标叠层 / Present（PR-H-video-split-2）
 *
 * 从 Video.c 迁出；只搬家、不改逻辑。
 */
#include "VideoPrivate.h"
#include "Scheduler.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

/* 单次 Present 条带行数：cli 下 memcpy 过久会饿死 xHCI poll/MSI */
void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                           UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 X1;
    UINT32 Y1;

    if (gForceFront) {
        return;
    }
    if (!W || !H || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    X1 = X + W;
    Y1 = Y + H;
    if (X1 > gScreen.Width) {
        X1 = gScreen.Width;
    }
    if (Y1 > gScreen.Height) {
        Y1 = gScreen.Height;
    }
    if (X >= X1 || Y >= Y1) {
        return;
    }
    if (!*Dirty) {
        *Dx0 = X;
        *Dy0 = Y;
        *Dx1 = X1;
        *Dy1 = Y1;
        *Dirty = 1;
        return;
    }
    if (X < *Dx0) {
        *Dx0 = X;
    }
    if (Y < *Dy0) {
        *Dy0 = Y;
    }
    if (X1 > *Dx1) {
        *Dx1 = X1;
    }
    if (Y1 > *Dy1) {
        *Dy1 = Y1;
    }
}

void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (gCursorOverlay) {
        DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
        return;
    }
    DirtyUnionInto(&gDirty, &gDx0, &gDy0, &gDx1, &gDy1, X, Y, W, H);
}

void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
}

void VideoCursorOverlayBegin(void) {
    gCursorOverlay = 1;
}

void VideoCursorOverlayEnd(void) {
    gCursorOverlay = 0;
}

/* 将脏区 blit 到 GOP；无后缓冲时为空操作。scale=100 且逻辑=物理时 1:1 memcpy */
static void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
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

    if (!Scaled) {
        RowBytes = (UINT64)(X1 - X0) * 4ull;
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
            /* 条带间只 Restore IF（G7）；勿 SchedulerIoBreath——会 GuiPoll→Present 重入 */
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
        }
    }
}

void VideoPresent(void) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT64 FbBytes;
    UINT64 LayoutBytes;
    int Partial;

    if (!gBackOn || !gBack || !gFront) {
        gDirty = 0;
        gCurDirty = 0;
        return;
    }
    if (!gDirty && !gCurDirty) {
        return;
    }
    /* PR-K-preempt-cs：条带 cli 岛禁切（条间仍可 Restore IF） */
    SchedulerPreemptDisable();
    /*
     * 内容与光标分矩形 Present，避免 AABB 并成近全屏。
     * 大块按行条带 cli，条间开中断（G7：禁止长 cli 饿死 USB）。
     */
    LayoutBytes = (UINT64)gFrontPitch * (UINT64)gPhysH * 4ull;
    if (gPhysH == 0) {
        LayoutBytes = (UINT64)gFrontPitch * (UINT64)gScreen.Height * 4ull;
    }
    /*
     * 真机 GOP 常报 Size=Width*Height*4，但 Pitch>Width。
     * 若仍用 Size 做边界，Y 到中下部就 Partial 且永不前进 → 任务栏/底边永远不 Present。
     */
    FbBytes = gScreen.FrameBufferSize;
    if (FbBytes < LayoutBytes) {
        FbBytes = LayoutBytes;
    }

    if (gDirty) {
        X0 = gDx0;
        Y0 = gDy0;
        X1 = gDx1;
        Y1 = gDy1;
        gDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gDirty, &gDx0, &gDy0,
                            &gDx1, &gDy1, &Partial);
            if (Partial) {
                SchedulerPreemptEnable();
                return;
            }
        }
    }

    if (gCurDirty) {
        X0 = gCx0;
        Y0 = gCy0;
        X1 = gCx1;
        Y1 = gCy1;
        gCurDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gCurDirty, &gCx0, &gCy0,
                            &gCx1, &gCy1, &Partial);
        }
    }
    SchedulerPreemptEnable();
}

void VideoPresentFlush(void) {
    UINTN Guard;

    for (Guard = 0; Guard < 8192u; Guard++) {
        if (!gDirty && !gCurDirty) {
            return;
        }
        VideoPresent();
    }
    /* 仍脏：强制整屏再刷一轮（避免 4K 半截留下任务栏空洞） */
    if (gDirty || gCurDirty) {
        DirtyUnion(0, 0, gScreen.Width, gScreen.Height);
        for (Guard = 0; Guard < 8192u; Guard++) {
            if (!gDirty && !gCurDirty) {
                return;
            }
            VideoPresent();
        }
    }
}

void VideoSetPresentChunkRows(UINT32 Rows) {
    /* 0=默认；禁止全 1 作「无限」——与 Y 相加会回绕死循环 */
    if (Rows == 0xFFFFFFFFu) {
        Rows = 16384u;
    }
    gPresentChunkRows = Rows;
}

