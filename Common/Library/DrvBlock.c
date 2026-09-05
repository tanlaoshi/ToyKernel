/*
 * DrvBlock.c — Block 类适配层（PR-D2）
 */
#include "DrvBlock.h"
#include "Debug.h"

int ToyDrvBlockAttach(const BLOCK_BACKEND *Backend) {
    if (!Backend || !Backend->Probe || !Backend->ReadSectors || !Backend->WriteSectors) {
        DebugWrite("drv-block: bad backend\n");
        return -1;
    }
    BlockRegisterBackend(Backend);
    return 0;
}
