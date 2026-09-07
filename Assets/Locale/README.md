# Assets/Locale — 可编辑 UI 文案

改桌面程序名、菜单、提示语：**编辑本目录文本文件**，不必改 `Locale.c` / 重编内核。

| 文件 | 语言 |
|------|------|
| `en.txt` | 英文 |
| `zh.txt` | 中文 |

格式：`MSG_KEY=value`（`#` 行注释）。换行写成 `\n`。

```text
MSG_ICON_SHELL=Shell
MSG_ICON_SHELL=终端    # 在 zh.txt 里改
```

**语言选择**仍在 `TOYOS.DB`：`lang=en|zh`（Settings / `lang zh`）。

改完文件后，在 Guest 里执行：

```text
lang reload
```

图标位图仍在 `Assets/Icons/`，与文案无关。说明见 [`Documents/技术手册.md`](../../Documents/技术手册.md)。
