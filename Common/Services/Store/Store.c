/*
 * Store.c — PR-S1：离线 catalog 安装；PR-S3：font/asset → Assets/
 *           PR-S4：ToyDB 已装清单 + store remove
 *           PR-M1：depends=（catalog 第 8 段 / PKG.TXT）；缺依赖拒绝安装
 *           PR-M2：store combo / uncombo — 按依赖顺序装卸多包「功能」
 *
 * 载荷查找顺序：StoreCache/<file> → Assets/Store/packages/<id>/<file> → 卷根 <file>
 *
 * 卸装删的是 Apps/（或 Fonts/Packs）里的已装副本，不是 StoreCache/ 仓库。
 * 只删 U 盘 StoreCache 目录下的 ELF：商店目录仍在（catalog）；已装的仍在 Apps/；
 * 卷根还有 HELLO.ELF 等教学镜像，Install 仍可能成功。
 * sha256=- 时跳过校验（教学默认）。
 * 清单键：si.<id>=type|file ；依赖 sd.<id>=逗号 id 或 -
 * catalog 在 StoreCatalog.c。安装在 StoreInstall.c。组合包在 StoreCombo.c。卸装在 StoreRemove.c。查询在 StoreQuery.c / StoreManaged.c。
 * 依赖帮手：StoreDepends.c；清单键：StoreDb.c。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Theme.h"
#include "Scheduler.h"

/* 内核任务栈仅 8KiB；catalog 表放 BSS，避免 store sync/HTTP 栈溢出闪退 */
STORE_ENTRY gStoreTab[STORE_ENTRIES_MAX];
/* combo 嵌套卸装时合并 FontReload，避免连删字体卡死/重入 */
int gStoreComboDepth;
int gNeedFontReload;

/*
 * 长 IO 呼吸：每块拷贝/写盘后排空 xHCI 事件环 + 让鼠标动。
 *
 * PR-S-input-drain：稳态 drain 在 YieldForPollInput（shell/gui 每轮让步处）；
 * 但长 Store 拷贝/写盘期间 GuiTask 不走 YieldForPollInput，且真机 poll-USB 下
 * MSC/FAT 完成事件需 XchiDrainEvents 推进 → 此处自带 HalInputPoll 兜底。
 * 序 1「或等价」：drain 集中在 yield 路径 + IO 呼吸两处，GuiPollMouse 等只 dequeue。
 *
 * cli 守 drain：Shell 经 HalCpuHalt（sti;hlt）后常带 IF=1；HalInputPoll 调用链深，
 * 若被 LAPIC timer 嵌套（InterruptDispatch→SchedulerOnTimer）会压坏 iret 帧 →
 * #UD（见 InputTask scoped-sti 注释；QEMU smp=2 上 uncombo+combo guidemo 复现）。
 */
void StoreIoBreath(void) {
    UINT64 Flags;

    /* PR-K-preempt-cs：深 drain 禁切；与 cli 双保险 */
    SchedulerPreemptDisable();
    Flags = HalIrqSave();
    HalInputPoll();
    GuiPollMouseMotion();
    HalIrqRestore(Flags);
    SchedulerPreemptEnable();
}

void StoreFlushFontReload(void) {
    if (!gNeedFontReload) {
        return;
    }
    gNeedFontReload = 0;
    StoreIoBreath();
    (void)FontReloadAssets();
    ThemeClampFontId();
    /* 字高变了须重合成，否则桌面仍按旧度量画 */
    GuiComposeThemeScene();
    /* PR-S-compose-sep（序 4）：合成后、写盘前呼吸一次，避免 compose+ThemeSave
     * 连续长消费段冻住光标；ThemeSave 内 DB 写盘走 FatSetIoBreath 持续呼吸 */
    StoreIoBreath();
    FatSetIoBreath(StoreIoBreath);
    (void)ThemeSave();
    FatSetIoBreath(0);
    StoreIoBreath();
}

STORE_ENTRY *StoreScratchTab(void) {
    return gStoreTab;
}


/* FAT 上文件名大小写不一（HELLO.ELF vs hello.elf） */
int DirHasFileCI(const char *Dir, const char *File) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    int N = 0;
    int i;

    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, File)) {
            return 1;
        }
    }
    return 0;
}

/* 返回目录里真实名（大小写以盘为准），供 Delete 路径 */
int DirResolveFileCI(const char *Dir, const char *File, char *Out, int OutMax) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    char Want[STORE_FILE_MAX];
    int N = 0;
    int i;
    int w;

    if (!Out || OutMax <= 0) {
        return 0;
    }
    Out[0] = 0;
    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    /* File 与 Out 可能同缓冲：先拷走 */
    w = 0;
    while (File[w] && w + 1 < (int)sizeof(Want)) {
        Want[w] = File[w];
        w++;
    }
    Want[w] = 0;
    if (Want[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, Want)) {
            int k = 0;
            while (Ents[i].Name[k] && k + 1 < OutMax) {
                Out[k] = Ents[i].Name[k];
                k++;
            }
            Out[k] = 0;
            return Out[0] != 0;
        }
    }
    return 0;
}


/* 规范化：空 / "-" → 空串（表示无依赖） */
void NormalizeDepends(char *Dep) {
    if (!Dep) {
        return;
    }
    if (Dep[0] == 0 || (Dep[0] == '-' && Dep[1] == 0)) {
        Dep[0] = 0;
    }
}

int ArchOk(const char *Arch) {
    const char *Host;

    if (!Arch || Arch[0] == 0 || StrEq(Arch, "any")) {
        return 1;
    }
    Host = StoreHostArch();
    return StrEq(Arch, Host);
}

static int EnsureDir(const char *Path) {
    int Err = FileSystemMakeDirectory(Path);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

int EnsureAppsDir(void) {
    return EnsureDir(STORE_APPS_DIR);
}

int EnsureFontsDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_FONTS_DIR);
}

int EnsurePacksDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_PACKS_DIR);
}

int EntryKind(const char *Type) {
    if (StrEq(Type, "font")) {
        return STORE_KIND_FONT;
    }
    if (StrEq(Type, "asset")) {
        return STORE_KIND_ASSET;
    }
    if (StrEq(Type, "lib") || StrEq(Type, "library")) {
        return STORE_KIND_LIB;
    }
    if (StrEq(Type, "app") || Type[0] == 0) {
        return STORE_KIND_APP;
    }
    return -1;
}
