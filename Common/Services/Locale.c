/*
 * Locale.c — 中英文 UI 字符串表（PR-I18N2）
 */
#include "Locale.h"
#include "Db.h"
#include "Desktop.h"
#include "Gui.h"
#include "Debug.h"

static LOC_LANG gLang = LOC_LANG_EN;

static const char *const gEn[MSG_COUNT] = {
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
    " 4. Display (reboot)",
    " 5. Language",
    "1-5 open  Esc/0 back",
    " 0. Back",
    "Saved. QEMU: quit+./run-split.sh",
    " 1. English",
    " 2. Chinese",
    "[Desktop bg]",
    "[Shell bg]",
    "[Font]",
    "[Display] reboot",
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
    "usage: lang en|zh\n",
    "lang: ",
    "lang set\n",
    "lang: bad value (use en|zh)\n",
};

static const char *const gZh[MSG_COUNT] = {
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
    " 4. 显示（重启）",
    " 5. 语言",
    "1-5 打开  Esc/0 返回",
    " 0. 返回",
    "已保存。QEMU: 退出后 ./run-split.sh",
    " 1. English",
    " 2. 中文",
    "[桌面背景]",
    "[外壳背景]",
    "[字体]",
    "[显示] 重启",
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
    "用法: lang en|zh\n",
    "语言: ",
    "语言已切换\n",
    "语言: 无效（用 en|zh）\n",
};

void LocaleInit(void) {
    char Val[DB_VAL_MAX];

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
