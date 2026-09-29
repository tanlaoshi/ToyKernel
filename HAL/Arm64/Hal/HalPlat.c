/*
 * HalPlat.c — PR-S3-hal-arm-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootInfo.h"

int HalHasFrameBuffer(void) {
    const BOOT_INFO *Info = BootInfoGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

int HalConsoleOnly(void) {
    /* 无 FB → 串口命令行靶（virt --headless / 未来 Duo S 等板包） */
    return HalHasFrameBuffer() ? 0 : 1;
}

int HalPinInteractiveToBootstrap(void) {
    /* PR-V-ap-interactive：解钉；AP OnTimer 切 shell/gui（input-fix 已铺路） */
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    /* virt 平台形状（协作调度 / 桌面模块表）；串口见 HalConsoleOnly */
    return 1;
}

int HalCpuIsHypervisor(void) {
    return 1; /* Arm virt 当作 hypervisor 环境 */
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalCpuHalt();
    }
}

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) { return "ARM64 (virt A14)"; }

UINT16 HalElfMachine(void) {
    return 183; /* EM_AARCH64 */
}

/* PR-A12：AArch64 ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 1027: /* R_AARCH64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 257:  /* R_AARCH64_ABS64 */
        return HAL_ELF_RELOC_ABS64;
    case 1025: /* R_AARCH64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 1026: /* R_AARCH64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 1024: /* R_AARCH64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    UINT8 *P = (UINT8 *)Addr;
    UINT8 *End;
    UINT64 Line = 64;

    if (!Addr || Size == 0) {
        return;
    }
    End = P + Size;
    for (; P < End; P += Line) {
        __asm__ volatile("dc cvau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    for (P = (UINT8 *)Addr; P < End; P += Line) {
        __asm__ volatile("ic ivau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish\n isb" ::: "memory");
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}
void HalDebugWriteHex32(UINT32 Value) {
    char Buf[9];
    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWrite(Buf);
}
void HalDebugHex64(UINT64 Value) {
    char Buf[17];
    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWrite(Buf);
}

/* HalCpuCount / Id / ticks / HalSmpStartApplicationProcessors → Smp.c（PR-A14） */
