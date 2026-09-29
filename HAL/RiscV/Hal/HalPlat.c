/*
 * HalPlat.c — PR-S3-hal-riscv-1：架构名 / ELF / 调试 / 平台探测
 */
#include "Hal.h"
#include "BootInfo.h"
#include "BoardConfig.h"

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
    /* virt 平台形状（协作调度 / 桌面模块表）；真机板包 IS_VIRT=0（PR-B3） */
#if defined(TOY_BOARD_IS_VIRT) && TOY_BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

int HalCpuIsHypervisor(void) {
#if defined(TOY_BOARD_IS_VIRT) && TOY_BOARD_IS_VIRT
    return 1;
#else
    return 0;
#endif
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalCpuHalt();
    }
}

const char *HalArchName(void) { return "riscv64"; }
const char *HalCpuInfo(void) { return "RISC-V (virt A14)"; }

UINT16 HalElfMachine(void) {
    return 243; /* EM_RISCV */
}

/* PR-A12：RISC-V ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 3: /* R_RISCV_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 2: /* R_RISCV_64 */
        return HAL_ELF_RELOC_ABS64;
    case 5: /* R_RISCV_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 4: /* R_RISCV_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    (void)Addr;
    (void)Size;
    __asm__ volatile("fence.i" ::: "memory");
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
