# ToyOS PMM 稀疏化（内存上限 256GB）

> **状态**：已落地（2026-09-24）。256 段 × 1GB。分配只返回恒等窗口内的页：x86 512MB，arm64/riscv 4GB。  
> **前置**：cap = `PHYSICAL_MEMORY_MAX_PAGES` 256K 页 = 1 GiB。  
> **目标**：分段稀疏位图，跟踪上限 256GB。静态 BSS 增量约 552KB（段表 8KB + 段 0 位图 32KB + 段 0 refcount 512KB）。  
> **相关**：`Core/PhysicalMemory.c` · `Core/PhysicalMemorySeg.c` · `Core/PhysicalMemoryAlloc.c` · `Include/PhysicalMemory.h` · `Include/PhysicalMemoryPrivate.h`  
> **命名**：PascalCase；新 `.c` ≤300；三架构可编。文件留在 `Core/`，不迁到 `Common/Core/`。

---

## 〇、已确认决策

| # | 决策 | 结论 |
| - | ---- | ---- |
| 1 | 对外 API | `AllocatePages` / `FreePages` / `RetainPage` / `ReleasePage` / `TotalPages` / `FreePageCount` 签名不变。`RetainPage` 仍返回 `int` |
| 2 | 页大小 | 4KB（`PAGE_SHIFT=12`） |
| 3 | 调用方 | 现有调用方不动 |
| 4 | 段 | 1GB（shift 30），256 段 → 256GB |
| 5 | refcount | UINT16，饱和 `0xFFFF`。每段 512KB，段 0 静态，段 1+ 懒分配 |
| 6 | 段内 RAM | 位图初值全 1（已用），`Free=1` 区间逐页清位。不假设连续 |
| 7 | 连续分配 | 只在同一段内。跨段请求失败 |
| 8 | 鸡生蛋 | 静态位图挂在**内核所在段**（x86 为段 0，arm64 virt 为段 1）。其余段从 `KernelEnd` 在该段内 bump。分出的页在清完 Free 位之后再标已用 |
| 9 | 旧全局 | `gPhysBase` / `gMaxPage` 在第 2 刀退场。`gFreePages` 留到第 4 刀，第 2 刀起改为各段 `FreePages` 之和的镜像 |
| 10 | 第 1 刀 | 只填段表。不删 `gBitmap` / `gRefCount`，不改 Alloc/Free/Retain/Release |

实现时以上不得再议。若要改，先改本文。

---

## 一、边界

| 场景 | 行为 |
| ---- | ---- |
| `BootAllocate` 超出段 0 | `DebugWrite`，返回 NULL。v1 不扩到段 1 |
| 段 1+ 位图分配失败 | 该段 `PageCount=0`，跳过。不崩溃 |
| `AllocatePages(0)` | 返回 NULL |
| 连续页跨段 | 单段内找不到则失败 |
| `FreePages` / `Retain` / `Release` 打到未分配页或越界 | `DebugWrite`，返回。`RetainPage` 返回 -1 |
| refcount 饱和 | 停在 `0xFFFF` |
| `TotalPages` | 各段 `PageCount` 之和（第 4 刀） |
| `FreePageCount` | 各段 `FreePages` 之和（第 4 刀） |
| `Present` | 不调用 `SchedulerIoBreath` |

填段表时，**先**把每段 `BasePhys` 设成 `Seg << 30`，再算页索引。段 1+ 的 `BasePhys` 不能留 0，否则索引会按整段物理地址去算。

`BootAllocate` 标已用必须放在 `Free=1` 清位**之后**。内核末尾落在 Conventional 区里，先标已用会被后面的清位冲掉。

---

## 二、结构

```c
#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_COUNT     256
#define PMM_PAGES_PER_SEGMENT ((1ULL << PMM_SEGMENT_SHIFT) / PAGE_SIZE)

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount; /* 段内最高页索引 + 1；0 = 无 RAM */
    UINT32  FreePages;
    UINT8  *Bitmap;    /* 1=已用；每段 32KB */
    UINT16 *RefCount;  /* 每段 512KB */
} PMM_SEGMENT;
```

宏与结构放 `PhysicalMemory.c` 内部（或私有头）。不进 `PhysicalMemory.h` 的公开区。

寻址：`Seg = Phys >> 30`，`Idx = (Phys - BasePhys) >> 12`。

BSS：`gSegments[256]` ≈ 8KB，`gSegment0Bitmap` 32KB，`gSegment0RefCount` 512KB，合计约 552KB。

---

## 三、五刀

| 刀 | 交付 | 不动 | 验收 |
| -- | ---- | ---- | ---- |
| **1 PR-PMM-seg** ★ | 段表 + `BootAllocate` + 按 `Regions[]` 填 `PageCount`/`FreePages` + DEBUG `PmmDebugDump` | Alloc/Free/Retain/Release 仍走旧扁平位图 | 三架构编译；`smoke-boot` / `smoke-virt`；串口见 `pmm: seg N pages= free=` |
| **2 PR-PMM-alloc** | Alloc/Free 段内寻址。删 `ComputeBaseAndMax`、`gBitmap`、`gRefCount`、`gPhysBase`、`gMaxPage` | Retain/Release 仍走旧表，直到第 3 刀。`gFreePages` 暂留 | smoke；分配再释放，空闲数回得去 |
| **3 PR-PMM-ref** | Retain/Release 走段内 UINT16 | 签名不变（`RetainPage` 仍返回 int） | smoke；`FORK.ELF` 不崩 |
| **4 PR-PMM-count** | Total/Free 跨段相加；删 `gFreePages`；摘要 >1024 MiB 显示 GiB | — | QEMU 1G 摘要约 1024 MiB |
| **5 PR-PMM-arch** | 三架构 smoke；本文改「已落地」；路线图 §七归档 | 不改功能 | 三架构 PASS |

一次只做一刀。第 1 刀确认后再写代码。

---

## 四、明确不做

- 不改 syscall 号、页大小、公开函数签名
- 不做跨段连续分配
- 不把 `gTasks` 一类全局搬进 Modules（本柱与可替换模块化无关）
- 学生代码不进默认 Kernel
- 第 1 刀不删旧扁平位图
- 段 1+ 分配失败不崩溃
