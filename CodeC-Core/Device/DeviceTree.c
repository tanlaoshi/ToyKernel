/*
 * DeviceTree.c — PR-DEV-tree-api：设备父子拓扑 API（策略 A）。
 *
 * 只存 Parent（DEVICE_NODE.Parent 已有），不引入 Children[]/ChildCount。
 * 三个 API 全部用公开的 DeviceGet()/DeviceCount() 扫表，不碰 Core/Device.c
 * 的静态表，从而把拓扑逻辑独立成文件（Core/Device.c 已近 300 行）。
 *
 * 环检测：沿 Parent 链上行，若遇到 Child 自身则拒绝（避免成环）。
 */

#include "Device.h"
#include "Debug.h"

/* 判断 P 是否指向 gDevices[] 表内某节点。NULL 不算表内。 */
static int DeviceInTable(DEVICE_NODE *P) {
    int i;
    int N = DeviceCount();

    if (!P) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (DeviceGet(i) == P) {
            return 1;
        }
    }
    return 0;
}

int DeviceSetParent(DEVICE_NODE *Child, DEVICE_NODE *Parent) {
    DEVICE_NODE *Cur;

    if (!Child || !DeviceInTable(Child)) {
        return -1;
    }
    /* Parent==NULL：摘下，允许。非 NULL 必须在表内。 */
    if (Parent && !DeviceInTable(Parent)) {
        return -1;
    }
    /* 环检测：沿 Parent 链上行，遇到 Child 即成环。 */
    Cur = Parent;
    while (Cur) {
        if (Cur == Child) {
            return -1;
        }
        Cur = DeviceGetParent(Cur);
    }
    Child->Parent = Parent;
    return 0;
}

DEVICE_NODE *DeviceGetParent(DEVICE_NODE *Dev) {
    if (!Dev) {
        return 0;
    }
    return Dev->Parent;
}

int DeviceGetChildren(DEVICE_NODE *Parent, DEVICE_NODE **Out, int Max) {
    int i;
    int N = DeviceCount();
    int Found = 0;

    if (!Out || Max <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Node = DeviceGet(i);

        if (Node->Parent == Parent) {
            if (Found >= Max) {
                break;
            }
            Out[Found] = Node;
            Found++;
        }
    }
    return Found;
}

#if TOY_KERNEL_DEBUG
/* PR-DEV-tree-api：DEBUG 自检——SetParent 后 GetChildren 能找回，且环检测生效。
 * 用表中前两个 PCI 设备做一次挂接再还原；不影响后续驱动绑定（Parent 不参与匹配）。 */
void DeviceTreeSelfTest(void) {
    DEVICE_NODE *A = 0;
    DEVICE_NODE *B = 0;
    DEVICE_NODE *Old;
    DEVICE_NODE *Out[8];
    int i;
    int N = DeviceCount();
    int Ok = 1;

    for (i = 0; i < N && (!A || !B); i++) {
        DEVICE_NODE *Node = DeviceGet(i);

        if (Node->Bus != DEVICE_BUS_PCI) {
            continue;
        }
        if (!A) {
            A = Node;
        } else if (Node != A) {
            B = Node;
        }
    }
    if (!A || !B) {
        DebugWrite("tree-api: SKIP (need >=2 pci devs)\n");
        return;
    }
    Old = B->Parent;
    if (DeviceSetParent(B, A) != 0) {
        Ok = 0;
    }
    if (DeviceGetChildren(A, Out, 8) < 1 || Out[0] != B) {
        Ok = 0;
    }
    /* 环检测：把 A 挂到 B 下应被拒（A->B->A 成环） */
    if (DeviceSetParent(A, B) != -1) {
        Ok = 0;
    }
    /* 还原 */
    DeviceSetParent(B, Old);
    DeviceSetParent(A, Old);
    DebugWrite(Ok ? "tree-api: OK\n" : "tree-api: FAIL\n");
}
#else
void DeviceTreeSelfTest(void) {
}
#endif
