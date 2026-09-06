/*
 * DriverBlock.c — Block 类适配层（PR-D2）
 */
#include "DriverBlock.h"
#include "Debug.h"

int ToyDriverBlockAttach(const BLOCK_BACKEND *Backend) {
    if (!Backend || !Backend->Probe || !Backend->ReadSectors || !Backend->WriteSectors) {
        DebugWrite("drv-block: bad backend\n");
        return -1;
    }
    BlockRegisterBackend(Backend);
    return 0;
}
