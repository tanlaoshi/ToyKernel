# ToyOS 桌面 / UI 图标

统一风格图标，供下一步替换桌面 Shell / Settings / Files 色块，以及任务栏「开始」等。

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

### bmp48 文件名（FAT 友好）

| 文件 | 用途 |
|------|------|
| `SHELL.BMP` | 桌面 Shell |
| `SET.BMP` | Settings |
| `FILES.BMP` | Files |
| `START.BMP` | 开始菜单 / 任务栏 |
| `NET.BMP` | 网络（预留） |
| `POWER.BMP` | 电源（预留） |
| `HELP.BMP` / `INFO.BMP` / `HOME.BMP` / `CLOSE.BMP` | 预留 |

壁纸仍在 [`../Images/WALL.BMP`](../Images/WALL.BMP)。

## 下一步（未改代码）

在 `Desktop.c` 中用 `FsReadFile("Assets/Icons/bmp48/SHELL.BMP", …)` + `BmpDecode` 绘制图标，替代 `UiFillRectangle` 色块；缺失时回退现有色块即可。

rootfs 打包：确保 `ToyImage` / `prepare-virt-rootfs.sh` 把本目录拷进 Guest 的 `Assets/Icons/`。
