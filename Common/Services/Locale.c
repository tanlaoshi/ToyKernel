/*
 * Locale.c — UI 字符串：内建 fallback + Assets/Locale 文本覆盖（可编辑，不改源码）
 *
 * 语言选择仍经 TOYOS.DB 键 lang=en|zh。
 * 文案文件：Assets/Locale/en.txt、zh.txt（KEY=value，# 注释）。
 */
#include "Locale.h"
#include "Db.h"
#include "Desktop.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "Debug.h"

#define LOCALE_STR_MAX   96
#define LOCALE_FILE_MAX  (24u * 1024u)

static LOC_LANG gLang = LOC_LANG_EN;

/* 与 MSG_ID 顺序一致，供文本键名 */
static const char *const gMsgKeys[MSG_COUNT] = {
    "MSG_APP_SHELL",
    "MSG_APP_SETTINGS",
    "MSG_APP_FILES",
    "MSG_ICON_SHELL",
    "MSG_ICON_SETTINGS",
    "MSG_ICON_FILES",
    "MSG_START",
    "MSG_SET_TITLE",
    "MSG_SET_MAIN",
    "MSG_SET_DESKTOP_BG",
    "MSG_SET_SHELL_BG",
    "MSG_SET_FONT",
    "MSG_SET_DISPLAY",
    "MSG_SET_LANGUAGE",
    "MSG_SET_HINT_MAIN",
    "MSG_SET_HINT_BACK",
    "MSG_SET_SAVED",
    "MSG_SET_SAVED_PC",
    "MSG_SET_PREF_DIFF",
    "MSG_SET_PREF_DIFF_PC",
    "MSG_SET_LANG_EN",
    "MSG_SET_LANG_ZH",
    "MSG_SET_PAGE_DESKTOP",
    "MSG_SET_PAGE_SHELL",
    "MSG_SET_PAGE_FONT",
    "MSG_SET_PAGE_DISPLAY",
    "MSG_SET_PAGE_LANG",
    "MSG_FILES_VIEW",
    "MSG_FILES_NEW_DIR",
    "MSG_FILES_NEW_FILE",
    "MSG_FILES_RENAME",
    "MSG_FILES_PROMPT_HINT",
    "MSG_FILES_EMPTY",
    "MSG_FILES_EMPTY_HINT",
    "MSG_CON_WELCOME",
    "MSG_CON_READY",
    "MSG_LANG_USAGE",
    "MSG_LANG_NOW",
    "MSG_LANG_SET",
    "MSG_LANG_BAD",
};

static const char *const gEnFallback[MSG_COUNT] = {
    "ToyOS Shell",
    "Settings",
    "Files",
    "Shell",
    "Settings",
    "Files",
    "Start",
    "Settings",
    "[Main]",
    " 1. Desktop bg",
    " 2. Shell bg",
    " 3. Font",
    " 4. Display",
    " 5. Language",
    "1-5 open  Esc/0 back",
    " 0. Back",
    "Saved. Quit QEMU + ./run-split.sh",
    "Saved. Real PC uses monitor EDID/GOP",
    "Pref!=Now: quit QEMU + ./run-split.sh",
    "Pref recorded; real PC boot follows EDID",
    " 1. English",
    " 2. Chinese",
    "[Desktop bg]",
    "[Shell bg]",
    "[Font]",
    "[Display]",
    "[Language]",
    "View: ",
    "New directory",
    "New empty file",
    "Rename to",
    "Enter=ok  Esc=cancel  Backspace",
    "This folder is empty",
    "n = mkdir    f = new file",
    "ToyOS console. Type help.",
    "ToyOS ready. Type shell / settings, or any key to open Shell.",
    "usage: lang en|zh|reload\n",
    "lang: ",
    "lang set\n",
    "lang: bad value (use en|zh|reload)\n",
};

static const char *const gZhFallback[MSG_COUNT] = {
    "ToyOS 外壳",
    "设置",
    "文件",
    "外壳",
    "设置",
    "文件",
    "开始",
    "设置",
    "[主菜单]",
    " 1. 桌面背景",
    " 2. 外壳背景",
    " 3. 字体",
    " 4. 显示",
    " 5. 语言",
    "1-5 打开  Esc/0 返回",
    " 0. 返回",
    "已保存。请退出 QEMU 后 ./run-split.sh",
    "已保存。真机启动跟显示器 EDID/GOP",
    "Pref!=Now: 退出 QEMU 后 ./run-split.sh",
    "已记录偏好；真机启动跟 EDID",
    " 1. English",
    " 2. 中文",
    "[桌面背景]",
    "[外壳背景]",
    "[字体]",
    "[显示]",
    "[语言]",
    "查看: ",
    "新建目录",
    "新建空文件",
    "重命名为",
    "Enter=确认 Esc=取消 Backspace",
    "此文件夹为空",
    "n = 新建目录    f = 新建文件",
    "ToyOS 控制台。输入 help。",
    "ToyOS 就绪。输入 shell / settings，或按任意键打开外壳。",
    "用法: lang en|zh|reload\n",
    "语言: ",
    "语言已切换\n",
    "语言: 无效（用 en|zh|reload）\n",
};

static char gEn[MSG_COUNT][LOCALE_STR_MAX];
static char gZh[MSG_COUNT][LOCALE_STR_MAX];

static void CopyStrUnescape(char *Dst, UINTN DstMax, const char *Src) {
    UINTN i;
    UINTN o;

    if (!Dst || DstMax == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    o = 0;
    for (i = 0; Src[i] && o + 1 < DstMax; i++) {
        if (Src[i] == '\\' && Src[i + 1] == 'n') {
            Dst[o++] = '\n';
            i++;
        } else if (Src[i] == '\\' && Src[i + 1] == '\\') {
            Dst[o++] = '\\';
            i++;
        } else {
            Dst[o++] = Src[i];
        }
    }
    Dst[o] = 0;
}

static void ResetToFallback(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        CopyStrUnescape(gEn[i], LOCALE_STR_MAX, gEnFallback[i]);
        CopyStrUnescape(gZh[i], LOCALE_STR_MAX, gZhFallback[i]);
    }
}

static int KeyToId(const char *Key) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        const char *K = gMsgKeys[i];
        UINTN j;
        for (j = 0; K[j] && Key[j] && K[j] == Key[j]; j++) {
        }
        if (K[j] == 0 && Key[j] == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void ApplyLine(char (*Table)[LOCALE_STR_MAX], const char *Line) {
    char Key[48];
    const char *Eq;
    const char *Val;
    UINTN i;
    int Id;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#' || *Line == ';') {
        return;
    }
    Eq = Line;
    while (*Eq && *Eq != '=') {
        Eq++;
    }
    if (*Eq != '=' || Eq == Line) {
        return;
    }
    i = 0;
    while (Line < Eq && i + 1 < sizeof(Key)) {
        if (*Line != ' ' && *Line != '\t') {
            Key[i++] = *Line;
        }
        Line++;
    }
    Key[i] = 0;
    Val = Eq + 1;
    while (*Val == ' ' || *Val == '\t') {
        Val++;
    }
    Id = KeyToId(Key);
    if (Id < 0) {
        return;
    }
    CopyStrUnescape(Table[Id], LOCALE_STR_MAX, Val);
}

static int LoadCatalogFile(const char *Path, char (*Table)[LOCALE_STR_MAX]) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN i;
    UINTN LineStart;
    int Err;

    Pages = (LOCALE_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, LOCALE_FILE_MAX - 1, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Buf[Size] = 0;
    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            char Saved = (char)Buf[i];
            Buf[i] = 0;
            if (i > LineStart) {
                ApplyLine(Table, (const char *)&Buf[LineStart]);
            }
            Buf[i] = (UINT8)Saved;
            if (i < Size && Buf[i] == '\r' && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    return 0;
}

static void LoadCatalogs(void) {
    int EnOk;
    int ZhOk;

    ResetToFallback();
    EnOk = LoadCatalogFile("Assets/Locale/en.txt", gEn);
    ZhOk = LoadCatalogFile("Assets/Locale/zh.txt", gZh);
    if (EnOk == 0 || ZhOk == 0) {
        DebugWrite("locale: catalog ");
        DebugWrite(EnOk == 0 ? "en " : "");
        DebugWrite(ZhOk == 0 ? "zh " : "");
        DebugWrite("from Assets/Locale\n");
    } else {
        DebugWrite("locale: using built-in strings (no Assets/Locale)\n");
    }
}

void LocaleInit(void) {
    char Val[DB_VAL_MAX];

    LoadCatalogs();
    gLang = LOC_LANG_EN;
    if (DbGet("lang", Val, sizeof(Val)) == DB_OK) {
        if (Val[0] == 'z' && Val[1] == 'h') {
            gLang = LOC_LANG_ZH;
        } else if (Val[0] == 'e' && Val[1] == 'n') {
            gLang = LOC_LANG_EN;
        }
    }
    DebugWrite("locale: ");
    DebugWrite(gLang == LOC_LANG_ZH ? "zh\n" : "en\n");
}

void LocaleReload(void) {
    LoadCatalogs();
    LocaleApplyUi();
}

LOC_LANG LocaleGet(void) {
    return gLang;
}

int LocaleSet(LOC_LANG Lang) {
    int Rc;

    if (Lang != LOC_LANG_EN && Lang != LOC_LANG_ZH) {
        return -1;
    }
    gLang = Lang;
    Rc = DbSet("lang", Lang == LOC_LANG_ZH ? "zh" : "en");
    LocaleApplyUi();
    return Rc == DB_OK ? 0 : -1;
}

const char *LocStr(MSG_ID Id) {
    if ((UINT32)Id >= (UINT32)MSG_COUNT) {
        return "";
    }
    return (gLang == LOC_LANG_ZH) ? gZh[Id] : gEn[Id];
}

void LocaleApplyUi(void) {
    DesktopRefreshLabels();
    GuiRefreshTitles();
}
