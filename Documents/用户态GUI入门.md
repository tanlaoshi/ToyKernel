# 如何写第一个 GUI 程序（PR-L3）

> 目标：用 **libToyUi / libToyGfx** 写一个可 `exec` 的窗口程序。  
> 库本体 ✅ **G15**；CRT 模板 ✅ **L1**。分层约束见 [`架构分层.md`](架构分层.md) §9。

---

## 1. 一分钟跑通

```bash
cd ToyKernel && ./build.sh          # 生成 Kernel、CRT、libToyUi.a / libToyGfx.a、GUIDEMO.ELF
cd User/pkg/gui && make             # → build/MYGUI.ELF
cp build/MYGUI.ELF ../../../ToyImage/rootfs/MYGUI.ELF
# QEMU（run-split.sh）Shell 中：
#   toyos> exec MYGUI.ELF
```

或直接跑仓库自带演示：`exec GUIDEMO.ELF`（源码 `User/GuiDemo.c`）。

---

## 2. 最小程序

```c
#include <stdio.h>
#include <unistd.h>
#include <ToyUi.h>
#include <toyos/syscall.h>

int main(void) {
    int Wid = ToyUiCreateWindow("MyGui", 400, 240);
    if (Wid < 0) {
        return 1;
    }
    ToyUiSetLabel(Wid, "Hello GUI");
    ToyUiAddButton(Wid, 0, "OK");

    for (;;) {
        int Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            ToyUiSetLabel(Wid, "OK!");
        } else if (Ev == TOY_UI_EVENT_NONE) {
            toy_yield();   /* 无事件时让出 CPU */
        }
    }
    return 0;
}
```

链接（`User/pkg/gui/Makefile` 已配好）：

- `crt0.o` + `syscall.o` + `libtoyos.a`
- `libToyUi.a` + `libToyGfx.a`
- `User/user.ld`

---

## 3. ABI 一览（稳定面）

版本宏（破坏性变更升 **MAJOR**）：

| 库 | 宏 | 当前 |
|----|-----|------|
| libToyUi | `TOY_UI_ABI_VERSION_*` | **1.0.0** |
| libToyGfx | `TOY_GFX_ABI_VERSION_*` | **1.0.0** |

### libToyUi

| API | 语义 |
|-----|------|
| `ToyUiCreateWindow(title, w, h)` | 成功 `wid>=0`，失败 `-1` |
| `ToyUiSetLabel(wid, text)` | 客户区一行文字；成功 `0` |
| `ToyUiAddButton(wid, id, label)` | `id` ∈ **0..3**；底栏排布；成功 `0` |
| `ToyUiPoll(wid)` | 非阻塞；见下表 |

| `ToyUiPoll` 返回值 | 含义 |
|--------------------|------|
| `TOY_UI_EVENT_NONE` (0) | 无事件 |
| `TOY_UI_EVENT_CLOSE` (1) | 用户点了标题栏关闭 |
| `TOY_UI_BUTTON_EVENT(id)` (=100+id) | 按钮 `id` 被点击 |
| `-1` | 无效 wid / 内核错误 |

### libToyGfx

| API | 语义 |
|-----|------|
| `ToyGfxDamageText(wid, text)` | 文字 damage（`SYS_DAMAGE`）；成功 `0` |

本版 **没有** 像素 framebuffer / blit API（后置）；颜色常量仅作文档对照。

### 底层 syscall（一般不必直接调）

| 号 | 名 | 用户包装 |
|----|-----|----------|
| 18 | `SYS_CREATE_WINDOW` | `create_window` / `ToyUiCreateWindow` |
| 19 | `SYS_DAMAGE` | `damage` / `ToyGfxDamageText` |
| 20 | `SYS_POLL_INPUT` | `poll_input` / `ToyUiPoll` |
| 21 | `SYS_UI_BUTTON` | `ui_button` / `ToyUiAddButton` |

---

## 4. 课堂注意

- **禁止** `#include` 内核 `Include/`、调用 `Hal*`、链接 `Common/`。
- 焦点在用户窗时，`printf` **只走串口**，不会画进客户区（避免文字叠窗）。
- 关窗靠 `TOY_UI_EVENT_CLOSE`；进程应 `return`/`exit`，不要空转。
- 盘上 ELF 名建议 **8.3 大写**（如 `MYGUI.ELF`）。

---

## 5. 相关路径

| 路径 | 说明 |
|------|------|
| `User/include/ToyUi.h` / `ToyGfx.h` | ABI 头 |
| `User/Library/ToyUi/` / `ToyGfx/` | 源与 `.a` |
| `User/GuiDemo.c` → `GUIDEMO.ELF` | 官方演示 |
| `User/pkg/gui/` | 学生模板 |
| [`用户态库.md`](用户态库.md) | 库清单与排期 |
