# ToyOS 桌面 / UI 图标

统一风格图标：桌面 Shell / Settings / Files，任务栏「开始」与开始菜单项。

## 来源与许可

- 矢量源： [Lucide](https://lucide.dev/)（[ISC License](https://github.com/lucide-icons/lucide/blob/main/LICENSE)）
- 本目录对 SVG 做了重命名（`shell` ← `terminal`，`files` ← `folder`，`start` ← `layout-grid`，`help` ← `circle-question-mark` 等），并栅格化为 PNG/BMP
- 底板色块为 ToyOS 自行合成（圆角矩形 + 白色描边字形），便于当前 `Bmp.c`（BI_RGB、无透明通道）直接解码

ISC 允许商用/修改；保留 Lucide 版权声明见 [`LICENSE-Lucide.txt`](LICENSE-Lucide.txt)。

## 目录

| 路径 | 说明 |
|------|------|
| `svg/` | Lucide 原 SVG（描边风格统一） |
| `png48/` / `png32/` | 带底板的 PNG（预览 / 将来若支持透明） |
| `bmp48/` | **运行时首选**：48×48 BI_RGB BMP（对齐 `DESKTOP_ICON_SIZE`） |

### bmp48 文件名（FAT 友好）与 UI 映射

| 文件 | 用途 |
|------|------|
| `SHELL.BMP` | 桌面 Shell + 开始菜单项 |
| `SET.BMP` | Settings + 开始菜单项 |
| `FILES.BMP` | Files + 开始菜单项 |
| `START.BMP` | 任务栏「开始」钮（缩放到 20×20） |
| `NET.BMP` | 网络（预留） |
| `POWER.BMP` | 电源（预留） |
| `HELP.BMP` / `INFO.BMP` / `HOME.BMP` / `CLOSE.BMP` | 预留 |

运行时：`Desktop.c` 经 `FileSystemReadFile("Assets/Icons/bmp48/…")` + `BmpDecode` 绘制；缺失回退色块。

壁纸仍在 [`../Images/WALL.BMP`](../Images/WALL.BMP)。

## rootfs

`ToyImage/prepare-rootfs.sh` 与 `ToyKernel/prepare-virt-rootfs.sh` 将本目录同步到 Guest `Assets/Icons/`（与 `Assets/Images/` 同级）。
