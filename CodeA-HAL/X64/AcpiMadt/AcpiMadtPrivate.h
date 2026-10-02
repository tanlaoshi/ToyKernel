/*
 * AcpiMadtPrivate.h — 表头、映射与电源态（PR-S-acpimadt-1）
 */
#ifndef ACPI_MADT_PRIVATE_H
#define ACPI_MADT_PRIVATE_H

#include "AcpiMadt.h"
#include "Hal.h"
#include "VirtualMemory.h"

#define ACPI_MAP_FLAGS (PTE_PRESENT | PTE_WRITABLE)
#define ACPI_MAX_ROOT_ENTRIES 256u
#define ACPI_MAX_TABLE_BYTES  (256u * 1024u)

typedef struct {
    char   Signature[8];
    UINT8  Checksum;
    char   OemId[6];
    UINT8  Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress;
    UINT8  ExtendedChecksum;
    UINT8  Reserved[3];
} __attribute__((packed)) ACPI_RSDP;

typedef struct {
    char   Signature[4];
    UINT32 Length;
    UINT8  Revision;
    UINT8  Checksum;
    char   OemId[6];
    char   OemTableId[8];
    UINT32 OemRevision;
    UINT32 CreatorId;
    UINT32 CreatorRevision;
} __attribute__((packed)) ACPI_SDT_HEADER;

typedef struct {
    ACPI_SDT_HEADER Header;
    UINT32 LocalApicAddress;
    UINT32 Flags;
} __attribute__((packed)) ACPI_MADT;

#define PM1_PWRBTN_STS (1u << 8)
#define PM1_PWRBTN_EN  (1u << 8)
#define PM1_SLP_EN     (1u << 13)
#define PM1_SCI_EN     (1u << 0)
/* FADT Flags bit4：1=电源键仅 control-method（无固定功能位） */
#define FADT_FLAG_PWR_BUTTON (1u << 4)
/* FADT Flags bit10：RESET_REG 有效 */
#define FADT_FLAG_RESET_REG  (1u << 10)

extern UINT16 gPm1aEvt;
extern UINT16 gPm1aCnt;
extern UINT16 gPm1bEvt;
extern UINT16 gPm1bCnt;
extern UINT16 gPm1aEn;
extern UINT16 gPm1bEn;
extern UINT8 gPm1EvtLen;
extern UINT8 gSlpTypA;
extern UINT8 gPowerReady;
extern UINT8 gResetSpace;
extern UINT64 gResetAddr;
extern UINT8 gResetValue;
extern UINT8 gResetAccess;

int MemEq(const char *A, const char *B, int N);
int MapPhys(UINT64 Phys, UINTN Bytes);
ACPI_SDT_HEADER *MapSdtHeader(UINT64 Phys);
ACPI_SDT_HEADER *FindTableXsdt(ACPI_SDT_HEADER *Xsdt, const char *Sig);
ACPI_SDT_HEADER *FindTableRsdt(ACPI_SDT_HEADER *Rsdt, const char *Sig);
ACPI_MADT *FindMadt(UINT64 RsdpPhys);
ACPI_SDT_HEADER *FindDmar(UINT64 RsdpPhys);
ACPI_SDT_HEADER *FindFacp(UINT64 RsdpPhys);

UINT64 GasIoAddress(const UINT8 *Gas);
UINT64 GasAnyAddress(const UINT8 *Gas);
void PowerStallMs(UINT32 Ms);
void PowerBootLine(const char *Text);
void PowerBootHex(const char *Prefix, UINT32 Value);
void ParseSlpTypFromFacp(UINT64 RsdpPhys, ACPI_SDT_HEADER *Facp, UINT8 *P);
void Pm1EnablePowerButton(UINT16 EvtPort, UINT16 EnPort);
void Pm1WriteSleep(UINT16 CntPort, UINT8 Typ);

#endif
