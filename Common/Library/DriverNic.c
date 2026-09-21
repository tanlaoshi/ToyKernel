/*
 * DriverNic.c — NIC_L2 校验（PR-N-nic-l2）
 */
#include "DriverNic.h"

int NicL2OpsValid(const NIC_L2 *Nic) {
    if (!Nic || !Nic->SendFrame || !Nic->Poll || !Nic->GetMac) {
        return 0;
    }
    return 1;
}
