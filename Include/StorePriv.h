/*
 * StorePriv.h — Store 内部（仅 Common/Services/Store）
 *
 * 对外 API 仍在 Store.h。User 勿 include。
 */
#ifndef STORE_PRIV_H
#define STORE_PRIV_H

#include "Store.h"

#define STORE_KIND_APP     0
#define STORE_KIND_FONT    1
#define STORE_KIND_ASSET   2
#define STORE_KIND_LIB     3 /* 公共库：uncombo 不随 app 卸 */

extern STORE_ENTRY gStoreTab[STORE_ENTRIES_MAX];
extern int gStoreComboDepth;
extern int gNeedFontReload;

void NormalizeDepends(char *Dep);
void StoreIoBreath(void);
void StoreFlushFontReload(void);
int ArchOk(const char *Arch);
int EnsureAppsDir(void);
int EnsureFontsDir(void);
int EnsurePacksDir(void);
int EntryKind(const char *Type);
int LoadPkgDepends(const char *Id, char *Out, int OutMax);
int CheckDependsInstalled(const char *Depends);
int MakeDbKey(char *Out, int Max, const char *Prefix, const char *Id);
int StoreHasSi(const char *Id);
int StoreAdoptInstalled(const char *Id);
int ResolveEntryDepends(const char *Id, char *OutDepends, int OutMax);
int CollectDependents(const char *Id, char OutIds[][STORE_ID_MAX], int Max);
int LookupPackageKind(const char *Id);
int DirHasFileCI(const char *Dir, const char *File);
int DirResolveFileCI(const char *Dir, const char *File, char *Out, int OutMax);

/* 与 FilesUi 的同名函数签名不同，不能做成全局符号。 */
static inline void CopyStr(char *Dst, int DstMax, const char *Src) {
    int i = 0;

    if (!Dst || DstMax <= 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i + 1 < DstMax) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static inline void JoinPath(char *Dst, int DstMax, const char *A, const char *B) {
    int i = 0;
    int j = 0;

    if (!Dst || DstMax <= 0) {
        return;
    }
    while (A && A[i] && i + 1 < DstMax) {
        Dst[i] = A[i];
        i++;
    }
    if (i > 0 && Dst[i - 1] != '/' && i + 1 < DstMax) {
        Dst[i++] = '/';
    }
    while (B && B[j] && i + 1 < DstMax) {
        Dst[i++] = B[j++];
    }
    Dst[i] = 0;
}

#endif
