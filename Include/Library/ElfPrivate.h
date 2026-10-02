/*
 * ElfPrivate.h — Elf 内部分文件共用（仅 Library/Elf；User 勿 include）
 *
 * 对外 API 仍在 Elf.h。
 */
#ifndef ELF_PRIVATE_H
#define ELF_PRIVATE_H

#include "Elf.h"

void ElfZeroMemory(void *Ptr, UINTN Size);
int ElfStrEq(const char *A, const char *B);
int ElfHeaderOk(const Elf64_Ehdr *Hdr, UINTN Size, UINT16 WantType);
UINT64 ElfAlignUp(UINT64 Value, UINT64 Align);
int ElfMapSegment(VIRTUAL_ADDRESS_SPACE *Space, const UINT8 *Image,
                  const Elf64_Phdr *Ph, UINT64 Bias);
int ElfMapStack(VIRTUAL_ADDRESS_SPACE *Space);
int ElfPhdrs(const UINT8 *Bytes, UINTN Size, const Elf64_Ehdr *Hdr,
             const Elf64_Phdr **OutPh, UINT16 *OutN);
int ElfVaToFileOff(const Elf64_Phdr *Ph, UINT16 N, UINT64 Va, UINT64 Bias,
                   UINT64 *Off, UINTN ImageSize);
const Elf64_Dyn *ElfFindDynamic(const UINT8 *Bytes, UINTN Size,
                                const Elf64_Phdr *Ph, UINT16 N,
                                UINT64 *DynBytes);

#endif
