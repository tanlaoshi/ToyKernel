# Assets/Fonts — 运行时点阵（PR-T3）

Guest 路径：`Assets/Fonts/*.FNT`。启动时（FS 就绪后）`FontLoadAssets` 尝试加载：

1. `VGA8X16.FNT`（样本路径名；内容为 Linux `font_sun8x16` 8×16，GPL-2.0）
2. `EXTRA.FNT`（可选第二包）

缺文件或坏包 → **仅用内建** Terminus（16×32 / ×2 / 10×18）。Shell：`font` / `font reload`。

## TOYF v1（小端）

| 偏移 | 内容 |
|------|------|
| 0 | magic `TOYF` |
| 4 | `version=1`, `scale`, `char_spacing`, `line_spacing`（各 u8） |
| 8 | `width`, `height`, `first_char`, `glyph_count`（各 u16 LE） |
| 16 | `name[16]` NUL 填充 |
| 32 | 字形：`glyph_count × height × ceil(width/8)` 字节，MSB 在左 |

内建点阵仍在仓库 `Fonts/*.c`；本目录不链入 `Kernel.elf`。详见 [`Documents/字体与多语言.md`](../../Documents/字体与多语言.md)。
