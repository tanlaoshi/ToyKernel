/*
 * Arch.c — x86-64 GDT/TSS/SYSCALL 与 ArchInit（PR-S3-arch-1）
 *
 * 中断 IDT/PIC/LAPIC/分发见 ArchInterrupt.c。
 */
#include "Arch.h"
#include "ArchPrivate.h"
#include "Hal.h"
#include "Debug.h"
#include "BootTypes.h"
#include "IoApic.h"

typedef struct {
    UINT16 Limit;
    UINT64 Base;
} __attribute__((packed)) DT_PTR;

static UINT64 gGdt[5 + HAL_MAX_CPUS * 2];

typedef struct __attribute__((packed)) {
    UINT32 Reserved0;
    UINT64 RSP0;
    UINT64 RSP1;
    UINT64 RSP2;
    UINT64 Reserved1;
    UINT64 IST1;
    UINT64 IST2;
    UINT64 IST3;
    UINT64 IST4;
    UINT64 IST5;
    UINT64 IST6;
    UINT64 IST7;
    UINT64 Reserved2;
    UINT16 Reserved3;
    UINT16 IOPBOffset;
} TSS64;

static TSS64 gTss[HAL_MAX_CPUS] __attribute__((aligned(16)));
static UINT8 gKernelIstStack[HAL_MAX_CPUS][16384] __attribute__((aligned(16)));

/* SYSCALL 入口用：KERNEL_GS_BASE 指向本结构；与 int 0x80 无关 */
typedef struct {
    UINT64 KernelRsp;      /* gs:[0] — 与 TSS.RSP0 同步 */
    UINT64 ScratchUserRsp; /* gs:[8] — SyscallEntry 暂存用户 RSP */
} ARCH_CPU_LOCAL;

static ARCH_CPU_LOCAL gArchCpuLocal[HAL_MAX_CPUS] __attribute__((aligned(16)));

#define MSR_EFER            0xC0000080u
#define MSR_STAR            0xC0000081u
#define MSR_LSTAR           0xC0000082u
#define MSR_SFMASK          0xC0000084u
#define MSR_GS_BASE         0xC0000101u
#define MSR_KERNEL_GS_BASE  0xC0000102u
#define EFER_SCE            (1ULL << 0)
/* SFMASK：进入 SYSCALL 时清 IF（bit9），与 SyscallDispatch 关中断一致 */
#define SFMASK_IF           (1ULL << 9)

extern void SyscallEntry(void);

static void Wrmsr(UINT32 Msr, UINT64 Value) {
    UINT32 Lo = (UINT32)Value;
    UINT32 Hi = (UINT32)(Value >> 32);
    __asm__ volatile ("wrmsr" :: "c"(Msr), "a"(Lo), "d"(Hi) : "memory");
}

static UINT64 Rdmsr(UINT32 Msr) {
    UINT32 Lo;
    UINT32 Hi;
    __asm__ volatile ("rdmsr" : "=a"(Lo), "=d"(Hi) : "c"(Msr));
    return ((UINT64)Hi << 32) | Lo;
}

/*
 * 配置本核 SYSCALL/SYSRET MSR。
 * STAR：kernel CS=0x08（SS=0x10）；user base=0x13 → SYSRET CS=0x23 SS=0x1B
 * （依赖 GDT：1=kcode 2=kdata 3=udata 4=ucode）
 */
void ArchSyscallMsrInit(UINT32 LogicalCpu) {
    UINT64 Efer;
    UINT64 Star;
    UINT64 Local;

    if (LogicalCpu >= HAL_MAX_CPUS) {
        LogicalCpu = 0;
    }

    gArchCpuLocal[LogicalCpu].KernelRsp = gTss[LogicalCpu].RSP0;

    Local = (UINT64)(UINTN)&gArchCpuLocal[LogicalCpu];
    Wrmsr(MSR_GS_BASE, 0);
    Wrmsr(MSR_KERNEL_GS_BASE, Local);

    Star = (0x0013ULL << 48) | (0x0008ULL << 32);
    Wrmsr(MSR_STAR, Star);
    Wrmsr(MSR_LSTAR, (UINT64)(UINTN)SyscallEntry);
    Wrmsr(MSR_SFMASK, SFMASK_IF);

    Efer = Rdmsr(MSR_EFER);
    Wrmsr(MSR_EFER, Efer | EFER_SCE);
}

static void TssDescWrite(UINT32 Cpu) {
    UINT64 TssBase = (UINT64)(UINTN)&gTss[Cpu];
    UINT32 TssLimit = sizeof(TSS64) - 1;
    UINT32 Idx = 5 + Cpu * 2;

    gGdt[Idx] = (TssLimit & 0xFFFFULL)
            | ((TssBase & 0xFFFFFFULL) << 16)
            | (0x89ULL << 40)
            | ((UINT64)(TssLimit & 0x000F0000ULL) << 32)
            | ((TssBase & 0xFF000000ULL) << 32);
    gGdt[Idx + 1] = (TssBase >> 32) & 0xFFFFFFFFULL;
}

static UINT16 TssSelector(UINT32 Cpu) {
    return (UINT16)(0x28 + Cpu * 16);
}

/* 构造 GDT：内核段、用户段、每核 TSS；far return 刷新段寄存器 */
static void GdtLoad(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)(sizeof(gGdt) / sizeof(gGdt[0])); i++) {
        gGdt[i] = 0;
    }
    gGdt[1] = 0x00AF9A000000FFFFULL;
    gGdt[2] = 0x00CF92000000FFFFULL;
    gGdt[3] = 0x00CFF2000000FFFFULL;
    gGdt[4] = 0x00AFFA000000FFFFULL;

    for (i = 0; i < HAL_MAX_CPUS; i++) {
        gTss[i].RSP0 =
            (UINT64)(UINTN)(gKernelIstStack[i] + sizeof(gKernelIstStack[i]));
        gArchCpuLocal[i].KernelRsp = gTss[i].RSP0;
        TssDescWrite(i);
    }

    DT_PTR Ptr;
    Ptr.Limit = (UINT16)(sizeof(gGdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gGdt;

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :
        : "m"(Ptr)
        : "rax", "memory"
    );
}

/* 每核 TSS.RSP0：用户态陷入内核时用的栈（int 0x80 与 SYSCALL 共用） */
void ArchSetRsp0(UINT64 Rsp0) {
    UINT32 Cpu = HalCpuGetId();
    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    gTss[Cpu].RSP0 = Rsp0;
    gArchCpuLocal[Cpu].KernelRsp = Rsp0;
}

void ArchTssInstall(void) {
    gTss[0].RSP0 =
        (UINT64)(UINTN)(gKernelIstStack[0] + sizeof(gKernelIstStack[0]));
    gArchCpuLocal[0].KernelRsp = gTss[0].RSP0;
    TssDescWrite(0);
    __asm__ volatile ("ltr %%ax" :: "a"(TssSelector(0)));
}

/* 完整 CPU 中断环境初始化；自 IPI 测试成功返回 0 */
int ArchInit(void) {
    HalFpuEnableThisCpu();
    GdtLoad();
    ArchTssInstall();
    ArchPicMaskAll();
    ArchLapicEnable();
    ArchIdtLoad();
    (void)IoApicInit(); /* PR-H-ioapic：掩 RTE；设备路由在驱动里 */
    __asm__ volatile ("sti");
    ArchLapicSelfIpi(VEC_XHCI);
    for (volatile int i = 0; i < 1000000; i++) {
        if (gArchIrqCount) {
            break;
        }
    }
    __asm__ volatile ("cli");
    DebugWrite("IDT ready, LAPIC on, self-IPI ok=");
    DebugHex32(gArchIrqCount);
    DebugWrite("\n");
    return gArchIrqCount > 0 ? 0 : -1;
}

/*
 * AP 初始化（PR-S2/S4）：
 * 切到共享 gGdt（含每核 TSS），ltr 本核 TSS，再 lidt + 开 LAPIC。
 */
void ArchApInit(UINT32 LogicalCpu) {
    DT_PTR Ptr;

    if (LogicalCpu >= HAL_MAX_CPUS) {
        LogicalCpu = HAL_MAX_CPUS - 1;
    }

    gTss[LogicalCpu].RSP0 = (UINT64)(UINTN)(
        gKernelIstStack[LogicalCpu] + sizeof(gKernelIstStack[LogicalCpu]));
    gArchCpuLocal[LogicalCpu].KernelRsp = gTss[LogicalCpu].RSP0;
    TssDescWrite(LogicalCpu);

    Ptr.Limit = (UINT16)(sizeof(gGdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gGdt;
    __asm__ volatile ("lgdt %0" : : "m"(Ptr) : "memory");

    __asm__ volatile (
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :
        :
        : "rax", "memory"
    );

    __asm__ volatile ("ltr %%ax" :: "a"(TssSelector(LogicalCpu)));

    ArchIdtLidt();
    ArchLapicEnable();
    ArchSyscallMsrInit(LogicalCpu);
    HalPatApplyWc(); /* 与 BSP 同形：PA1=WC，LFB WC 映射对其它核也生效 */
    HalFpuEnableThisCpu();
}

/* 开中断 */
void ArchSti(void) {
    __asm__ volatile ("sti");
}

/* 关中断 */
void ArchCli(void) {
    __asm__ volatile ("cli");
}
