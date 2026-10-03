/*
 * PageAbout.c — 关于页（策略 A 1200–1299；导航 9000 独立）
 * PR-UI-page-2：§2.3 表驱动页内分发（逻辑 ID → 函数指针，无 if-else）
 *              + §5 设计器空壳样式（控件声明 / 分发表 / 动作桩）
 *
 * 设计器生成边界（§5）：
 *   - ID 枚举（Ids.h 1200–1299）        ← 设计器按页生成
 *   - 控件声明（AboutCreate 里的 Add*）  ← 设计器按布局生成
 *   - 分发表的「事件列」（gAboutMap[]）  ← 设计器按 ID 生成
 *   - 动作桩声明 + 空实现（OnAboutXxx）  ← 设计器生成空壳，学生填 TODO
 */
#include <ToyUi.h>
#include "Ids.h"
#include "PageAbout.h"

/* §5 动作桩：设计器生成空实现，学生只需填 TODO 处的业务逻辑 */
static void OnAboutInfo(int Wid) {
    /* TODO: 真实业务（如弹说明、切子页） */
    ToyUiSetLabel(Wid, "About: info 1200");
}

static void OnAboutHelp(int Wid) {
    /* TODO: 真实业务 */
    ToyUiSetLabel(Wid, "About: help 1201");
}

/* §2.3 分发表：逻辑 ID → 处理函数指针。
 * 控件多时（§2.3 适用于 >30）避免巨型 if-else；此处 2 个示意，扩到 N 个同形。 */
typedef struct {
    int Id;
    void (*Handler)(int Wid);
} ID_HANDLER;

static const ID_HANDLER gAboutMap[] = {
    { ID_ABOUT_INFO, OnAboutInfo },
    { ID_ABOUT_HELP, OnAboutHelp },
};

void AboutCreate(int Wid) {
    ToyUiSetLabel(Wid, "About 1200  nav=9002");
    ToyUiAddButton(Wid, 0, "Info");
    ToyUiAddButton(Wid, 1, "Help");
    ToyUiAddButton(Wid, 2, "->Home");
    ToyUiAddButton(Wid, 3, "->Set");
}

void AboutDestroy(int Wid) {
    /* 切页前清标签；按钮槽由下页 Create 覆盖 */
    ToyUiSetLabel(Wid, "");
}

void AboutDispatch(int Wid, int Id) {
    int i;
    int N = (int)(sizeof(gAboutMap) / sizeof(gAboutMap[0]));

    for (i = 0; i < N; i++) {
        if (gAboutMap[i].Id == Id) {
            gAboutMap[i].Handler(Wid);
            return;
        }
    }
    /* 未命中：设计器空壳下可留空；此处静默忽略 */
}
