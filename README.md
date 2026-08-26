# ToyKernel

ToyOS 内核，运行在 UEFI 环境，支持图形界面和输入。

## 功能特性

- **帧缓冲驱动** - 支持 32 位颜色，任意分辨率
- **点阵字体** - 8x16 VGA 字体，ASCII 32~126
- **UI 图形库** - 直线、矩形、圆角矩形、圆形、三角形（描边和填充）
- **键盘输入** - 使用 UEFI ConIn 协议，支持特殊键（回车、退格、ESC 等）
- **鼠标输入** - 使用 UEFI SimplePointer 协议，支持相对移动和按键

## 文件说明

| 文件 | 说明 |
|------|------|
| `BootConfig.h` | 从 ToyBoot 接收的结构体定义 |
| `Kernel.c` / `Kernel.h` | 内核入口 |
| `Video.c` / `Video.h` | 帧缓冲驱动 + 字体渲染 |
| `UI.c` / `UI.h` | 图形库 + UI 组件 |
| `Input.c` / `Input.h` | 键盘 + 鼠标输入 |
| `FontData.h` | 8x16 VGA 点阵字体数据 |
| `link.ld` | 链接脚本 |
| `build.sh` | 编译脚本 |

## 编译

```bash
./build.sh