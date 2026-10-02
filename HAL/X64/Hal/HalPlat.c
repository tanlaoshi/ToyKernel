/*
 * HalPlat.c — PR-S3-hal-x64-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootInfo.h"

const char *HalArchName(void) {
    return "x86_64";
}

const char *HalCpuInfo(void) {
    return "x86-64 (ToyOS HAL)";
}

/* PR-A4：ELF e_machine = EM_X86_64 */
UINT16 HalElfMachine(void) {
    return 62;
}

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 8:  /* R_X86_64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 1:  /* R_X86_64_64 */
        return HAL_ELF_RELOC_ABS64;
    case 6:  /* R_X86_64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 7:  /* R_X86_64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 5:  /* R_X86_64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    (void)Addr;
    (void)Size;
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}

void HalDebugWriteHex32(UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWrite(Buf);
}

void HalDebugHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWrite(Buf);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}

int HalHasFrameBuffer(void) {
    const BOOT_INFO *Info = BootInfoGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

int HalConsoleOnly(void) {
    /* x86 课堂 / 真机桌面路径：始终走全量表，不用串口子集 */
    return 0;
}

int HalPinInteractiveToBootstrap(void) {
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    return 0;
}

int HalCpuIsHypervisor(void) {
    UINT32 Eax;
    UINT32 Ebx;
    UINT32 Ecx;
    UINT32 Edx;

    __asm__ volatile("cpuid"
                     : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx)
                     : "a"(1)
                     : "memory");
    (void)Eax;
    (void)Ebx;
    (void)Edx;
    return (Ecx & (1u << 31)) != 0;
}

void HalVirtPlatformIdleLoop(void) {
    for (;;) {
        HalCpuPark();
    }
}

void HalTimerPoll(void) {
}

/* SmpBoot.c 提供 HalCpuCount / HalCpuGetId / HalSmpStartApplicationProcessors */

void HalSmpNoteDtb(UINT64 DtbPhys) {
    (void)DtbPhys;
}
