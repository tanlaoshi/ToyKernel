/*
 * FileSystemPriv.h — FileSystem 内部（仅 Common/Services/FileSystem）
 *
 * 对外 API 仍在 FileSystem.h。User 勿 include。
 */
#ifndef FILE_SYSTEM_PRIV_H
#define FILE_SYSTEM_PRIV_H

#include "FileSystem.h"
#include "Vfs.h"

typedef struct {
    UINT32 Drive;
    UINT32 StartLba;
    char   Name[FS_VOL_NAME_MAX]; /* 前缀名，不含冒号：TOYOS / A / ESP / RES */
    char   Letter;                /* 'A'+index，便于 A: 访问 */
    int    ReadOnly;
    int    HasToyId;
    const FS_OPS *Ops;            /* PR-F3：每卷后端（fat / res） */
} FS_VOLUME;

extern FS_VOLUME gVols[FS_MAX_VOLUMES];
extern int gVolCount;
extern int gDefaultVol;
extern int gActiveVol;
extern UINT32 gActiveDrive;
extern UINT32 gActiveLba;
extern const FS_OPS *gActiveOps;

int MountAllVolumes(void);

/* FatPath.c 已有全局 StrEqIgnoreCase，这里不能再导出。 */
static inline int StrEqIgnoreCase(const char *A, const char *B) {
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static inline void CopyName(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

#endif
