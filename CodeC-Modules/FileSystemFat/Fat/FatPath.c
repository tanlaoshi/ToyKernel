/*
 * FatPath.c — 路径解析（PR-S-fatpath-1）
 * 名称匹配：FatPathName.c
 */
#include "Fat.h"
#include "FatPrivate.h"

int CopyPathComponent(const char **Path, char *Out, int OutMax) {
    const char *S = *Path;
    int n = 0;

    while (*S == '/') {
        S++;
    }
    if (!*S) {
        return 0;
    }
    while (*S && *S != '/') {
        if (n + 1 >= OutMax) {
            return 0;
        }
        Out[n++] = *S++;
    }
    Out[n] = 0;
    *Path = S;
    return n > 0;
}

int LookupInDir(FAT_DIR_CTX Dir, const char *Name,
                       UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr) {
    return ForEachDir(Dir, 0, Name, OutCluster, OutSize, OutAttr);
}

int ResolvePathAsDir(const char *Path, FAT_DIR_CTX *OutDir) {
    FAT_DIR_CTX Stack[FAT_PATH_DEPTH];
    int Sp = 0;
    char Comp[FAT_NAME_MAX + 1];
    const char *P = Path ? Path : "";

    Stack[0] = FatRootCtx();
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        UINT32 SubCluster = 0;
        UINT32 SubSize = 0;
        UINT8 Attr = 0;

        if (CompIsDot(Comp)) {
            continue;
        }
        if (CompIsDotDot(Comp)) {
            if (Sp > 0) {
                Sp--;
            }
            continue;
        }
        if (!LookupInDir(Stack[Sp], Comp, &SubCluster, &SubSize, &Attr)) {
            return 0;
        }
        if (!(Attr & FAT_ATTR_DIR)) {
            return 0;
        }
        if (Sp + 1 >= FAT_PATH_DEPTH) {
            return 0;
        }
        Sp++;
        Stack[Sp].IsFat16Root = 0;
        Stack[Sp].Cluster = SubCluster;
    }
    *OutDir = Stack[Sp];
    return 1;
}

int ResolvePathParentLeaf(const char *Path, FAT_DIR_CTX *OutParent, char *Leaf) {
    FAT_DIR_CTX Stack[FAT_PATH_DEPTH];
    int Sp = 0;
    char Comp[FAT_NAME_MAX + 1];
    const char *P = Path;
    char Last[FAT_NAME_MAX + 1];
    int HasLast = 0;
    int i;

    if (!Path || !Path[0]) {
        return 0;
    }
    Stack[0] = FatRootCtx();
    Last[0] = 0;
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        if (!*P || (*P == '/' && P[1] == 0)) {
            for (i = 0; i <= FAT_NAME_MAX; i++) {
                Last[i] = Comp[i];
                if (!Comp[i]) {
                    break;
                }
            }
            HasLast = 1;
            break;
        }
        if (CompIsDot(Comp)) {
            continue;
        }
        if (CompIsDotDot(Comp)) {
            if (Sp > 0) {
                Sp--;
            }
            continue;
        }
        {
            UINT32 SubCluster = 0;
            UINT32 SubSize = 0;
            UINT8 Attr = 0;
            if (!LookupInDir(Stack[Sp], Comp, &SubCluster, &SubSize, &Attr)) {
                return 0;
            }
            if (!(Attr & FAT_ATTR_DIR)) {
                return 0;
            }
            if (Sp + 1 >= FAT_PATH_DEPTH) {
                return 0;
            }
            Sp++;
            Stack[Sp].IsFat16Root = 0;
            Stack[Sp].Cluster = SubCluster;
        }
    }
    if (!HasLast) {
        return 0;
    }
    for (i = 0; i <= FAT_NAME_MAX; i++) {
        Leaf[i] = Last[i];
        if (!Last[i]) {
            break;
        }
    }
    *OutParent = Stack[Sp];
    return 1;
}
