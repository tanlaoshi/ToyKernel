/*
 * FatPriv.h — FAT 实现内部共享（PR-R3 拆分 Fat.c）
 *
 * 仅 Common/Library/Fat*.c 使用；公开 API 仍见 Fat.h。
 */
#ifndef FAT_PRIV_H
#define FAT_PRIV_H

#include "Fat.h"

#define SECTOR 512
#define FAT32_EOC 0x0FFFFFF8u
#define FAT16_EOC 0xFFF8u
#ifndef FAT_WRITE_MAX
#define FAT_WRITE_MAX (8 * 1024 * 1024)
#endif
#define FAT_NAME_MAX 255
#define FAT_PATH_DEPTH 16
#define FAT_ATTR_RO   0x01
#define FAT_ATTR_HID  0x02
#define FAT_ATTR_SYS  0x04
#define FAT_ATTR_VOL  0x08
/* FAT_ATTR_DIR 见 Fat.h */
#define FAT_ATTR_ARCH 0x20
#define FAT_ATTR_LFN  0x0F

typedef struct {
    int      IsFat16Root;
    UINT32   Cluster;
} FAT_DIR_CTX;

typedef struct {
    char   Name[FAT_NAME_MAX + 1];
    int    Expect;   /* 下一期望 ordinal；0=无进行中的 LFN */
    UINT8  Cksum;
    int    Valid;
} FAT_LFN_ACC;

/* 卷几何与扇区/簇缓冲（定义于 FatIo.c） */
extern UINT8 gSector[SECTOR];
extern UINT8 gCluster[SECTOR * 128];
extern UINT32 gStartLba;
extern UINT32 gBytesPerSector;
extern UINT32 gSectorsPerCluster;
extern UINT32 gReservedSectors;
extern UINT32 gNumFats;
extern UINT32 gSectorsPerFat;
extern UINT32 gRootCluster;
extern UINT32 gRootLba;
extern UINT32 gRootSectors;
extern UINT32 gFatStart;
extern UINT32 gDataStart;
extern UINT32 gFatType;
extern UINT32 gMaxCluster;

/* 目录枚举输出（定义于 FatDir.c） */
extern FAT_DIR_ENT *gListOut;
extern int gListMax;
extern int gListCount;

/* —— FatIo.c —— */
FAT_DIR_CTX FatRootCtx(void);
UINT16 Read16(const UINT8 *P);
UINT32 Read32(const UINT8 *P);
void Write16(UINT8 *P, UINT16 V);
void Write32(UINT8 *P, UINT32 V);
int LoadSector(UINT32 Lba);
int StoreSector(UINT32 Lba);
int LoadCluster(UINT32 Cluster);
int StoreCluster(UINT32 Cluster);
UINT32 ClusterBytes(void);
UINT32 FatNext(UINT32 Cluster);
int FatSet(UINT32 Cluster, UINT32 Value);
int ClusterEnd(UINT32 Cluster);
UINT32 EocValue(void);
int FatFreeChain(UINT32 Cluster);
UINT32 FatAllocCluster(void);

/* —— FatPath.c —— */
char ToUpper(char C);
int StrEqIgnoreCase(const char *A, const char *B);
int CompIsDot(const char *N);
int CompIsDotDot(const char *N);
int CompIsDotOrDotDot(const char *N);
UINT8 Fat83Checksum(const UINT8 *Name83);
int EntryIsLfn(const UINT8 *E);
int EntryIsVol(const UINT8 *E);
void ShortNameFromEntry(const UINT8 *E, char *Out, int OutMax);
int NameEqShort(const UINT8 *Entry, const char *Name);
int PathTo83(const char *Path, UINT8 Out[11]);
void LfnAccClear(FAT_LFN_ACC *A);
void LfnPutUcs(FAT_LFN_ACC *A, int Index, UINT16 U);
int LfnFeed(FAT_LFN_ACC *A, const UINT8 *E);
int EntryDisplayName(const UINT8 *E, const FAT_LFN_ACC *A, char *Out, int OutMax);
int EntryMatchesName(const UINT8 *E, const FAT_LFN_ACC *A, const char *Name);
UINT32 EntryCluster(const UINT8 *E);
int CopyPathComponent(const char **Path, char *Out, int OutMax);
int LookupInDir(FAT_DIR_CTX Dir, const char *Name,
                UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr);
int ResolvePathAsDir(const char *Path, FAT_DIR_CTX *OutDir);
int ResolvePathParentLeaf(const char *Path, FAT_DIR_CTX *OutParent, char *Leaf);

/* —— FatDir.c —— */
void PrintEntryNamed(const char *Name, UINT8 Attr);
int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
                  UINT32 *OutCluster, UINT32 *OutSize, int ListOnly,
                  UINT8 *OutAttr, FAT_LFN_ACC *Acc);
int ForEachDir(FAT_DIR_CTX Dir, int ListOnly, const char *Name,
               UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr);
int DirBufferHasEntries(UINT8 *Buf, UINT32 Bytes);
int DirIsEmpty(FAT_DIR_CTX Dir);
int DirGetEntryPos(FAT_DIR_CTX Dir, UINT32 Index, UINT32 *Lba, UINT32 *Off);
int DirReadEntry(FAT_DIR_CTX Dir, UINT32 Index, UINT8 Out[32]);
int DirWriteEntry(FAT_DIR_CTX Dir, UINT32 Index, const UINT8 Ent[32]);
int DirMaxIndex(FAT_DIR_CTX Dir);
int NameIsDot(const UINT8 *E);
int FindDirIndex(FAT_DIR_CTX Dir, const char *Name, int Need,
                 int *OutIndex, int *FoundExisting,
                 UINT32 *OldCluster, UINT8 *OldAttr);
int LfnEntryCountForName(const char *Name);
void FillLfnEntry(UINT8 *E, int Ord, int IsLast, UINT8 Cksum, const char *Name);
int Make83Alias(FAT_DIR_CTX Dir, const char *LongName, UINT8 Out[11]);
int DeleteLfnPrefix(FAT_DIR_CTX Dir, int SfnIndex, UINT8 Cksum);
int FindFreeRun(FAT_DIR_CTX Dir, int Need, int *OutIndex);
int DirGrow(FAT_DIR_CTX Dir);
int DirFindSlots(FAT_DIR_CTX Dir, int Need, int *OutIndex);
void FillSfnEntry(UINT8 *E, const UINT8 Name83[11], UINT8 Attr, UINT32 Cluster,
                  UINT32 Size);
UINT32 ParentClusterForDotDot(FAT_DIR_CTX Parent);
int DirCreateEntry(FAT_DIR_CTX Parent, const char *Leaf, UINT8 Attr,
                   UINT32 Cluster, UINT32 Size);

/* —— FatFile.c —— */
int ReadFileClusters(UINT32 Cluster, UINT32 Size, void *Buffer, UINTN MaxSize,
                     UINTN *OutSize);
int DirUnlinkKeepClusters(FAT_DIR_CTX Parent, const char *Leaf);

#endif
