/*
 * DbPriv.h — Db 内部（仅 Common/Services/Db）
 *
 * 对外 API 仍在 Db.h。User 勿 include。
 */
#ifndef DB_PRIV_H
#define DB_PRIV_H

#include "Db.h"

typedef struct {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int  Used;
} DB_REC;

extern DB_REC gRecs[DB_MAX_RECORDS];
extern int gReady;
extern int gDbDirty;
extern int gBatch;

static inline int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static inline int KeyOk(const char *K) {
    int N = 0;
    if (!K || !K[0]) {
        return 0;
    }
    while (K[N] && N < DB_KEY_MAX - 1) {
        char C = K[N];
        int Ok = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                 (C >= '0' && C <= '9') || C == '_' || C == '.' || C == '-';
        if (!Ok) {
            return 0;
        }
        N++;
    }
    return K[N] == 0;
}

static inline void CopyStr(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static inline int StrEq(const char *A, const char *B) {
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

int FindSlot(const char *Key);
int AllocSlot(void);
void ClearAll(void);
void ImportThemeCfgIfEmpty(void);

#endif
