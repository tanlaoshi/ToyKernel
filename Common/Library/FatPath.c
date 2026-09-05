/*
 * FatPath.c — 路径解析、名称/LFN 匹配
 */
#include "Fat.h"
#include "FatPriv.h"

char ToUpper(char C) {
    if (C >= 'a' && C <= 'z') {
        return (char)(C - 'a' + 'A');
    }
    return C;
}

int StrEqIgnoreCase(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        if (ToUpper(*A) != ToUpper(*B)) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

int CompIsDot(const char *N) {
    return N && N[0] == '.' && N[1] == 0;
}

int CompIsDotDot(const char *N) {
    return N && N[0] == '.' && N[1] == '.' && N[2] == 0;
}

int CompIsDotOrDotDot(const char *N) {
    return CompIsDot(N) || CompIsDotDot(N);
}

UINT8 Fat83Checksum(const UINT8 *Name83) {
    UINT8 Sum = 0;
    int i;
    for (i = 0; i < 11; i++) {
        Sum = (UINT8)(((Sum & 1) ? 0x80u : 0u) + (Sum >> 1) + Name83[i]);
    }
    return Sum;
}

int EntryIsLfn(const UINT8 *E) {
    return (E[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN;
}

int EntryIsVol(const UINT8 *E) {
    return (E[11] & FAT_ATTR_VOL) && !EntryIsLfn(E);
}

void ShortNameFromEntry(const UINT8 *E, char *Out, int OutMax) {
    int n = 0;
    int j;

    if (OutMax <= 0) {
        return;
    }
    for (j = 0; j < 8 && E[j] != ' '; j++) {
        if (n + 1 >= OutMax) {
            break;
        }
        Out[n++] = (char)E[j];
    }
    if (E[8] != ' ' && n + 1 < OutMax) {
        Out[n++] = '.';
        for (j = 0; j < 3 && E[8 + j] != ' '; j++) {
            if (n + 1 >= OutMax) {
                break;
            }
            Out[n++] = (char)E[8 + j];
        }
    }
    Out[n] = 0;
}

int NameEqShort(const UINT8 *Entry, const char *Name) {
    char FatName[13];
    ShortNameFromEntry(Entry, FatName, sizeof(FatName));
    return StrEqIgnoreCase(FatName, Name);
}

/* Path -> 11 字节 8.3；失败返回 0（需走 LFN） */
int PathTo83(const char *Path, UINT8 Out[11]) {
    int i;
    int bi = 0;
    int ei = 0;
    int seenDot = 0;

    for (i = 0; i < 11; i++) {
        Out[i] = ' ';
    }
    if (!Path || !Path[0]) {
        return 0;
    }
    for (i = 0; Path[i]; i++) {
        char C = Path[i];
        if (C == '.') {
            if (seenDot) {
                return 0;
            }
            seenDot = 1;
            continue;
        }
        C = ToUpper(C);
        if (!((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
              C == '_' || C == '-' || C == '~')) {
            return 0;
        }
        if (!seenDot) {
            if (bi >= 8) {
                return 0;
            }
            Out[bi++] = (UINT8)C;
        } else {
            if (ei >= 3) {
                return 0;
            }
            Out[8 + ei++] = (UINT8)C;
        }
    }
    return bi > 0 ? 1 : 0;
}

void LfnAccClear(FAT_LFN_ACC *A) {
    A->Name[0] = 0;
    A->Expect = 0;
    A->Cksum = 0;
    A->Valid = 0;
}

void LfnPutUcs(FAT_LFN_ACC *A, int Index, UINT16 U) {
    if (Index < 0 || Index >= FAT_NAME_MAX) {
        return;
    }
    if (U == 0xFFFF) {
        return;
    }
    if (U == 0) {
        A->Name[Index] = 0;
        return;
    }
    if (U < 0x80) {
        A->Name[Index] = (char)U;
    } else if (U < 0x100) {
        A->Name[Index] = (char)U;
    } else {
        A->Name[Index] = '?';
    }
}

int LfnFeed(FAT_LFN_ACC *A, const UINT8 *E) {
    int Ord = E[0] & 0x1F;
    int Base;
    int k;
    static const int Offs[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };

    if (Ord == 0) {
        LfnAccClear(A);
        return 1;
    }
    if (E[0] & 0x40) {
        LfnAccClear(A);
        A->Expect = Ord;
        A->Cksum = E[13];
        A->Valid = 1;
        for (k = 0; k <= FAT_NAME_MAX; k++) {
            A->Name[k] = 0;
        }
    } else if (!A->Valid || Ord != A->Expect || E[13] != A->Cksum) {
        LfnAccClear(A);
        return 1;
    }
    Base = (Ord - 1) * 13;
    for (k = 0; k < 13; k++) {
        UINT16 U = Read16(E + Offs[k]);
        LfnPutUcs(A, Base + k, U);
        if (U == 0) {
            break;
        }
    }
    A->Expect = Ord - 1;
    return 1;
}

int EntryDisplayName(const UINT8 *E, const FAT_LFN_ACC *A, char *Out, int OutMax) {
    if (A && A->Valid && A->Expect == 0 && Fat83Checksum(E) == A->Cksum && A->Name[0]) {
        int i;
        for (i = 0; i < OutMax - 1 && A->Name[i]; i++) {
            Out[i] = A->Name[i];
        }
        Out[i] = 0;
        return 1;
    }
    ShortNameFromEntry(E, Out, OutMax);
    return 0;
}

int EntryMatchesName(const UINT8 *E, const FAT_LFN_ACC *A, const char *Name) {
    char Disp[FAT_NAME_MAX + 1];

    if (NameEqShort(E, Name)) {
        return 1;
    }
    EntryDisplayName(E, A, Disp, sizeof(Disp));
    return StrEqIgnoreCase(Disp, Name);
}

UINT32 EntryCluster(const UINT8 *E) {
    UINT32 Cl = Read16(E + 26);
    if (gFatType == 32) {
        Cl |= (UINT32)Read16(E + 20) << 16;
    }
    return Cl;
}
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
