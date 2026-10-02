/*
 * DtbPrivate.h — Dtb / DtbProbe 内部交接（PR-S3-dtb-1）
 */
#ifndef DTB_PRIVATE_H
#define DTB_PRIVATE_H

#include "BootTypes.h"

#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  0x1u
#define FDT_END_NODE    0x2u
#define FDT_PROP        0x3u
#define FDT_NOP         0x4u
#define FDT_END         0x9u

typedef struct {
    UINT32 Magic;
    UINT32 Totalsize;
    UINT32 OffDtStruct;
    UINT32 OffDtStrings;
    UINT32 OffMemRsvmap;
    UINT32 Version;
    UINT32 LastCompVersion;
    UINT32 BootCpuidPhys;
    UINT32 SizeDtStrings;
    UINT32 SizeDtStruct;
} FDT_HEADER;

UINT32 DtbBe32(const void *P);
int DtbStrEq(const char *A, const char *B);
UINT32 DtbAlign4(UINT32 Off);

#endif
