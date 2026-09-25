/*
 * ProcessPrivate.h — 进程加载内部（仅 Core/Process；User 勿 include）
 */
#ifndef PROCESS_PRIVATE_H
#define PROCESS_PRIVATE_H

#include "Elf.h"
#include "VirtualMemory.h"

int ProcessStartElf(VIRTUAL_ADDRESS_SPACE *Space, const ELF_LOAD_RESULT *Info,
                    const char *Name);
int ProcessLoadPath(const char *Path, VIRTUAL_ADDRESS_SPACE **OutSpace,
                    ELF_LOAD_RESULT *OutInfo);
void ProcessStopAllUsers(void);

#endif
