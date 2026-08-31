/*
 * Arch.c — x86-64 架构初始化与中断分发
 *
 * 职责：加载 GDT/IDT、屏蔽 8259 PIC、启用 LAPIC、配置周期定时器。
 * 中断入口在 Isr.S；本文件实现 InterruptDispatch 与各设备 IRQ 处理逻辑。
 */
#include "Arch.h"
#include "Console.h"
#include "Serial.h"
#include "Debug.h"
#include "XHCI.h"
#include "Scheduler.h"
#include "BootConfig.h"
#include "Syscall.h"

extern void Isr128(void);

extern void (*IsrException[32])(void);
extern void (*IsrPic[16])(void);
extern void Isr64(void);
extern void Isr65(void);
extern void Isr255(void);

typedef struct {
    UINT16 Limit;
    UINT64 Base;
} __attribute__((packed)) DT_PTR;

typedef struct {
    UINT16 OffLo;
    UINT16 Selector;
    UINT8  Ist;
    UINT8  Type;
    UINT16 OffMid;
    UINT32 OffHi;
    UINT32 Zero;
} __attribute__((packed)) IDT_GATE;

static UINT64 gGdt[7];
static IDT_GATE gIdt[256] __attribute__((aligned(16)));

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

static TSS64 gTss __attribute__((aligned(16)));
static UINT8 gKernelIstStack[16384] __attribute__((aligned(16)));

/* 写 I/O 端口（字节） */
static inline void Outb(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

/* 构造 GDT：内核段、用户段、TSS；far return 刷新段寄存器 */
static void GdtLoad(void) {
    gGdt[0] = 0;
    gGdt[1] = 0x00AF9A000000FFFFULL;
    gGdt[2] = 0x00CF92000000FFFFULL;
    gGdt[3] = 0x00CFF2000000FFFFULL;
    gGdt[4] = 0x00AFFA000000FFFFULL;
    gGdt[5] = 0;
    gGdt[6] = 0;

    gTss.RSP0 = (UINT64)(UINTN)(gKernelIstStack + sizeof(gKernelIstStack));

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

/* 安装 TSS 并加载任务寄存器（进入用户态前调用） */
void ArchTssInstall(void) {
    gTss.RSP0 = (UINT64)(UINTN)(gKernelIstStack + sizeof(gKernelIstStack));
    UINT64 TssBase = (UINT64)(UINTN)&gTss;
    UINT32 TssLimit = sizeof(TSS64) - 1;
    gGdt[5] = (TssLimit & 0xFFFFULL)
            | ((TssBase & 0xFFFFFFULL) << 16)
            | (0x89ULL << 40)
            | ((UINT64)(TssLimit & 0x000F0000ULL) << 32);
    gGdt[6] = (TssBase >> 32) & 0xFFFFFFFFULL;
    __asm__ volatile ("ltr %%ax" :: "a"((UINT16)0x28));
}

/* 设置 IDT 门（Type: 0x8E=内核中断门, 0xEE=用户可调用中断门） */
void ArchIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type) {
    UINT64 Addr = (UINT64)(UINTN)Handler;
    gIdt[Vec].OffLo = (UINT16)Addr;
    gIdt[Vec].Selector = 0x08;
    gIdt[Vec].Ist = 0;
    gIdt[Vec].Type = Type;
    gIdt[Vec].OffMid = (UINT16)(Addr >> 16);
    gIdt[Vec].OffHi = (UINT32)(Addr >> 32);
    gIdt[Vec].Zero = 0;
}

static void IdtSet(UINT32 Vec, void *Handler) {
    ArchIdtSetGate(Vec, Handler, 0x8E);
}

/* 填充 IDT：CPU 异常、PIC、自定义 XHCI/定时器向量，然后 lidt */
static void IdtLoad(void) {
    UINT32 i;
    for (i = 0; i < 32; i++) {
        IdtSet(i, (void *)IsrException[i]);
    }
    for (i = 32; i < 48; i++) {
        IdtSet(i, (void *)IsrPic[i - 32]);
    }
    for (i = 48; i < 256; i++) {
        IdtSet(i, (void *)Isr255);
    }
    IdtSet(VEC_XHCI, (void *)Isr64);
    IdtSet(VEC_TIMER, (void *)Isr65);
    IdtSet(255, (void *)Isr255);

    DT_PTR Ptr;
    Ptr.Limit = (UINT16)(sizeof(gIdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gIdt;
    __asm__ volatile ("lidt %0" : : "m"(Ptr) : "memory");
}

/* 初始化并屏蔽 8259 双 PIC 所有 IRQ（使用 LAPIC 代替） */
static void PicMaskAll(void) {
    Outb(0x20, 0x11);
    Outb(0xA0, 0x11);
    Outb(0x21, 0x20);
    Outb(0xA1, 0x28);
    Outb(0x21, 0x04);
    Outb(0xA1, 0x02);
    Outb(0x21, 0x01);
    Outb(0xA1, 0x01);
    Outb(0x21, 0xFF);
    Outb(0xA1, 0xFF);
}

/* 向 8259 发送 EOI（从 PIC 兼容路径） */
static void PicEoi(UINT32 Irq) {
    if (Irq >= 8) {
        Outb(0xA0, 0x20);
    }
    Outb(0x20, 0x20);
}

#define LAPIC_BASE       0xFEE00000ULL
#define LAPIC_SVR        0xF0
#define LAPIC_EOI        0xB0
#define LAPIC_ICR_LO     0x300
#define LAPIC_ICR_HI     0x310
#define LAPIC_TPR        0x80
#define LAPIC_TIMER      0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_DIV  0x3E0

/* 写 LAPIC EOI 寄存器，表示中断处理完毕 */
void LapicEoi(void) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_EOI) = 0;
}

/* 通过 IA32_APIC_BASE MSR 启用本地 APIC 并设置 SVR */
static void LapicEnable(void) {
    UINT64 ApicBase;
    __asm__ volatile ("rdmsr" : "=A"(ApicBase) : "c"(0x1B));
    if (!(ApicBase & (1ULL << 11))) {
        ApicBase |= (1ULL << 11);
        __asm__ volatile ("wrmsr" ::"c"(0x1B), "A"(ApicBase));
    }

    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TPR) = 0;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_SVR) = (1u << 8) | 0xFF;
}

/* 发送 LAPIC 自 IPI，用于启动时验证 IDT 是否工作 */
static void LapicSelfIpi(UINT8 Vector) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_ICR_HI) = 0;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_ICR_LO) =
        (1u << 18) | (1u << 14) | Vector;
}

/* 配置 LAPIC 周期定时器，中断向量 VEC_TIMER，初始计数 50000 */
void TimerStart(void) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER_DIV) = 0xB;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER) =
        VEC_TIMER | (1u << 17);
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER_INIT) = 50000;
    DebugWrite("timer: LAPIC periodic vec=0x41\n");
}

/* 打印异常信息后 cli+hlt 死循环（#PF 时额外打印 CR2） */
static void ExceptionHalt(struct INT_FRAME *F) {
    ConsoleWrite("\nEXCEPTION vec=");
    ConsoleHex32((UINT32)F->Vector);
    ConsoleWrite(" err=");
    ConsoleHex32((UINT32)F->ErrorCode);
    ConsoleWrite(" rip=");
    ConsoleHex64(F->Rip);
    if (F->Vector == 14) {
        UINT64 Cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(Cr2));
        ConsoleWrite(" cr2=");
        ConsoleHex64(Cr2);
    }
    ConsoleWrite("\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static volatile UINT32 gIrqCount;

/*
 * 中断 C 分发入口（由 Isr.S 调用）
 * 返回值：0 表示不切换任务；非 0 为新任务 INT_FRAME 指针（切换 RSP）
 */
UINT64 InterruptDispatch(struct INT_FRAME *F) {
    if (F->Vector < 32) {
        ExceptionHalt(F);
    }
    if (F->Vector == VEC_XHCI) {
        gIrqCount++;
        XhciIrq();
        LapicEoi();
        return 0;
    }
    if (F->Vector == VEC_TIMER) {
        LapicEoi();
        return SchedulerOnTimer(F);
    }
    if (F->Vector == VEC_SYSCALL) {
        return SyscallDispatch(F);
    }
    if (F->Vector >= 32 && F->Vector < 48) {
        PicEoi((UINT32)(F->Vector - 32));
        LapicEoi();
        return 0;
    }
    LapicEoi();
    return 0;
}

/* 完整 CPU 中断环境初始化；自 IPI 测试成功返回 0 */
int ArchInit(void) {
    GdtLoad();
    PicMaskAll();
    LapicEnable();
    IdtLoad();
    __asm__ volatile ("sti");
    LapicSelfIpi(VEC_XHCI);
    for (volatile int i = 0; i < 1000000; i++) {
        if (gIrqCount) {
            break;
        }
    }
    __asm__ volatile ("cli");
    DebugWrite("IDT ready, LAPIC on, self-IPI ok=");
    DebugHex32(gIrqCount);
    DebugWrite("\n");
    return gIrqCount > 0 ? 0 : -1;
}

/* 开中断 */
void ArchSti(void) {
    __asm__ volatile ("sti");
}

/* 关中断 */
void ArchCli(void) {
    __asm__ volatile ("cli");
}
