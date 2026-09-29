/*
 * HAL/X64/Hal/Hal.c — PR-S3-hal-x64-1：CPU / IRQ / Timer / 用户地址布局
 *
 * 中断帧见 HalFrame.c；平台元信息见 HalPlat.c（同构切块供 arm/riscv）。
 */
#include "Hal.h"
#include "Arch.h"
#include "Debug.h"
#include "AcpiMadt.h"

int HalInit(void) {
    return ArchInit();
}

void HalCpuHalt(void) {
    /* 唤醒后保持 IF=1：若 hlt 后 cli，Shell 后续 NetPoll/绘屏整段关中断，
     * USB MSI 只在极短 hlt 窗口投递，宿主忙或 -smp 2 时易丢键鼠。 */
    __asm__ volatile ("sti; hlt" ::: "memory");
}

void HalCpuReboot(void) {
    UINT32 i;
    static UINT8 NullIdt[10];
    void *St;
    void *Rt;
    typedef void (*EfiResetSystemFn)(UINT32, UINT64, UINT64, void *)
        __attribute__((ms_abi));
    EfiResetSystemFn ResetFn;

    HalIrqDisable();

    /* 1) FADT RESET_REG（含 CF9 脉冲；写两次） */
    AcpiReset();

    /* 2) 无 FADT 也强制 CF9 warm→cold（Intel PCH 真机主路径） */
    AcpiCf9Reset(0x06);
    AcpiCf9Reset(0x0E);

    /* 3) 8042（有 KBC 的机器；USB 键盘机常无效） */
    for (i = 0; i < 100000; i++) {
        if ((HalIoRead8(0x64) & 0x02) == 0) {
            break;
        }
    }
    HalIoWrite8(0x64, 0xFE);
    for (i = 0; i < 200000; i++) {
        __asm__ volatile ("pause");
    }

    /* 4) port 0x92 */
    HalIoWrite8(0x92, (UINT8)(HalIoRead8(0x92) | 0x01));
    for (i = 0; i < 200000; i++) {
        __asm__ volatile ("pause");
    }

    /* 5) UEFI Runtime ResetSystem（冷复位）；放较后，避免与 GetTime 同类挂死抢前 */
    St = HalPlatformSystemTable();
    if (St != 0) {
        Rt = *(void **)(UINTN)((UINT8 *)St + 88);
        if (Rt != 0) {
            ResetFn = *(EfiResetSystemFn *)(UINTN)((UINT8 *)Rt + 104);
            if (ResetFn != 0) {
                ResetFn(0, 0, 0, 0);
            }
        }
    }

    /* 6) 三重故障：仅 hypervisor（QEMU）；真机 = CPU shutdown 假死 */
    if (HalCpuIsHypervisor()) {
        for (i = 0; i < 10; i++) {
            NullIdt[i] = 0;
        }
        __asm__ volatile ("lidt %0; ud2" :: "m"(NullIdt) : "memory");
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void HalCpuShutdown(void) {
    /* QEMU 口 + ACPI PM1 SLP_EN；失败则停机等长按 */
    AcpiPowerOff();
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

int HalPowerButtonPressed(void) {
    return AcpiPowerButtonPressed();
}

void HalIrqEnable(void) {
    ArchSti();
}

void HalIrqDisable(void) {
    ArchCli();
}

UINT64 HalIrqSave(void) {
    UINT64 Flags;

    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(Flags) :: "memory");
    return Flags;
}

void HalIrqRestore(UINT64 Flags) {
    if (Flags & (1ULL << 9)) {
        ArchSti();
    } else {
        ArchCli();
    }
}

void HalCpuRelax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    ArchIdtSetGate(Vector, Handler, Type);
}

void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    ArchIdtSetGate(Vector, Handler, 0x8E);
}

void HalIrqUnregister(UINT32 Vector) {
    ArchIdtSetGate(Vector, (void *)0, 0);
    (void)Vector;
}

void HalIrqEoi(UINT32 Vector) {
    (void)Vector;
    LapicEoi();
}

void HalTimerInitialize(void) {
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    (void)Milliseconds;
}

void HalTimerAck(void) {
    LapicEoi();
}

void HalTimerStart(void) {
    TimerStart();
}

/*
 * LAPIC INIT=50000、DIV=1：墙钟拍长随 APIC 总线变。
 * QEMU≈20000 拍/s（~50µs）；NUC 实测约 250 拍/s（~4ms，500ms 请求曾走到 4s）。
 */
UINT32 HalTicksPerSec(void) {
    static UINT32 Cached;

    if (Cached != 0) {
        return Cached;
    }
    if (HalCpuIsHypervisor()) {
        Cached = 20000;
    } else {
        Cached = 250;
    }
    return Cached;
}

void HalInstallUserMode(void) {
    ArchTssInstall();
}

void HalSyscallInit(void) {
    extern void Isr128(void);

    /* legacy：IDT 0x80，DPL=3（不进 Common） */
    ArchIdtSetGate(VEC_SYSCALL, (void *)Isr128, 0xEE);
    DebugWrite("syscall: vector 0x80 (DPL=3) ready\n");
    /* 快速路径 MSR（BSP）；AP 在 ArchApInit 中各自初始化 */
    ArchSyscallMsrInit(0);
    DebugWrite("syscall: SYSCALL/SYSRET MSR ready\n");
}

void HalSetKernelStack(UINT64 StackTop) {
    ArchSetRsp0(StackTop);
}

UINT64 HalUserCodeVirt(void) {
    return 0x40000000ULL;
}
UINT64 HalUserStackVirt(void) {
    return 0x40100000ULL;
}
UINT64 HalUserStackSize(void) {
    return 0x4000ULL;
}
UINT64 HalUserBrkMax(void) {
    return 0x40080000ULL;
}
UINT64 HalUserSoBase(void) {
    return 0x40080000ULL;
}
UINT64 HalUserMmapBase(void) {
    return 0x40200000ULL;
}
UINT64 HalUserMmapEnd(void) {
    return 0x40400000ULL; /* 2MiB 教学匿名区 */
}
UINT64 HalUserVirtEnd(void) {
    return HalUserMmapEnd();
}
void HalUserSelfTest(void) {
    /* x86 用户路径由 FAT ELF / runuser 覆盖；无需内嵌自测 */
}
