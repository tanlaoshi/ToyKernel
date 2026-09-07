# Assets/Fonts — 运行时点阵（PR-T3 / S3）

Guest 路径：`Assets/Fonts/*.FNT`。启动时（FS 就绪后）`FontLoadAssets`：

1. 优先加载样本 `VGA8X16.FNT`（内容为 Linux `font_sun8x16` 8×16，GPL-2.0）
2. 再扫描目录下其余 `*.FNT` / `*.fnt`（最多 `FONT_RUNTIME_MAX`=2 槽）

缺文件或坏包 → **仅用内建** Terminus（16×32 / ×2 / 10×18）。Shell：`font` / `font reload`。

**商店（PR-S3）**：`store install sun8` 将 `VGA8X16.FNT` 安装到本目录（覆盖同名样本，不产生第二字面）；安装后自动 reload。也可把 `VGA8X16.FNT` 直接放进镜像。

## TOYF v1（小端）

| 偏移 | 内容 |
|------|------|
| 0 | magic `TOYF` |
| 4 | `version=1`, `scale`, `char_spacing`, `line_spacing`（各 u8） |
| 8 | `width`, `height`, `first_char`, `glyph_count`（各 u16 LE） |
| 16 | `name[16]` NUL 填充 |
| 32 | 字形：`glyph_count × height × ceil(width/8)` 字节，MSB 在左 |

内建点阵仍在仓库 `Fonts/*.c`；本目录不链入 `Kernel.elf`。详见 [`Documents/技术手册.md`](../../Documents/技术手册.md)。
