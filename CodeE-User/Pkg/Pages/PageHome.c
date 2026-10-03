/*
 * PageHome.c — 首页（策略 A 1000–1099；导航 9000 独立）
 */
#include <ToyUi.h>
#include "Ids.h"
#include "PageHome.h"

void HomeCreate(int Wid) {
    ToyUiSetLabel(Wid, "Home 1000  nav=9000");
    ToyUiAddButton(Wid, 0, "Ping");
    ToyUiAddButton(Wid, 1, "->Set");
    ToyUiAddButton(Wid, 2, "->About");
}

void HomeDestroy(int Wid) {
    /* 切页前清标签；按钮槽由下页 Create 覆盖 */
    ToyUiSetLabel(Wid, "");
}

void HomeDispatch(int Wid, int Id) {
    if (Id == ID_HOME_PING) {
        ToyUiSetLabel(Wid, "Home ping 1000");
    }
}
