#!/usr/bin/env python3
# gen-cjk32.py — 汉字点阵（默认 18×18×4bpp + GB2312；与英文同量级、不拉伸）
# 宿主跑。源字体优先 Noto Sans CJK SC Bold。
"""Usage:
  python3 Tools/Scripts/gen-cjk32.py                 # GB2312 · 18×18×4bpp（默认）
  python3 Tools/Scripts/gen-cjk32.py --bpp 1         # 旧 1bpp
  python3 Tools/Scripts/gen-cjk32.py --dim 16        # 更小
  python3 Tools/Scripts/gen-cjk32.py --set ui
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
LOCALE = ROOT / "CodeD-Services/Locale/LocaleTableZh.c"
SCAN_ROOTS = [
    ROOT / "CodeD-Services",
    ROOT / "CodeB-Library",
    ROOT / "Include/Services",
]
FONT_CANDIDATES = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Medium.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
]

COMPUTER_VOCAB = """
绑定未全部部驱键鼠触屏分辨安装下载上传连接断开无线有线以太蓝牙
电源电池风扇温度频率文件夹路径扩展错误警告信息成功失败复制粘贴
剪切撤销重做搜索过滤排序刷新账户密码登录退出启动停止暂停继续
窗口按钮确定取消应用程序驱动键盘鼠标屏幕内存硬盘光驱主板芯片
固件固件内核模块进程线程任务调度中断缓存缓冲协议端口地址路由
网关域名解析防火墙代理服务客户端服务端主机访客虚拟机容器镜像
软件硬件固件升级降级备份还原同步共享权限管理员用户访客访客
只读写读写删除移动重命名新建打开关闭保存另存为打印预览缩放
全屏最小化最大化还原属性设置选项偏好配置参数默认高级基础
状态在线离线忙碌空闲就绪忙碌超时重试忽略忽略忽略忽略
声卡显卡网卡声道音量静音耳机扬声器摄像头麦克风打印机扫描仪
触摸板触控板滚动条标题栏工具栏状态栏侧边栏导航栏菜单栏
已绑定未绑定全部是否启用禁用可用不可用支持不支持兼容
左右上下前后内外大小多少高低快慢新旧开关通断有无对错
一二三四五六七八九十百千万亿零半双单多各级层条目次个
串口并口总线桥接控制器适配器扩展卡插槽接口插座插头
分区卷标格式化挂载卸载只读只写读写扇区磁道柱面簇
压缩解压加密解密签名校验哈希摘要摘要证书证书令牌
"""


def add_text_cps(cps: set[int], text: str) -> None:
    for ch in text:
        cp = ord(ch)
        if cp >= 0x80:
            cps.add(cp)


def parse_c_strings(body: str) -> list[str]:
    out: list[str] = []
    i = 0
    n = len(body)
    while i < n:
        if body[i] != '"':
            i += 1
            continue
        i += 1
        buf: list[str] = []
        while i < n:
            c = body[i]
            if c == "\\":
                nxt = body[i + 1] if i + 1 < n else ""
                if nxt == "n":
                    buf.append("\n")
                elif nxt == "t":
                    buf.append("\t")
                elif nxt in '\\"':
                    buf.append(nxt)
                else:
                    buf.append(nxt)
                i += 2
                continue
            if c == '"':
                i += 1
                break
            buf.append(c)
            i += 1
        out.append("".join(buf))
    return out


def zh_locale_codepoints() -> set[int]:
    text = LOCALE.read_text(encoding="utf-8")
    m = re.search(r"gZhFallback\[MSG_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("gZhFallback not found")
    cps: set[int] = set()
    for s in parse_c_strings(m.group(1)):
        add_text_cps(cps, s)
    return cps


def source_string_codepoints() -> set[int]:
    cps: set[int] = set()
    for root in SCAN_ROOTS:
        if not root.is_dir():
            continue
        for path in list(root.rglob("*.c")) + list(root.rglob("*.h")):
            try:
                body = path.read_text(encoding="utf-8")
            except OSError:
                continue
            for s in parse_c_strings(body):
                if any(ord(ch) >= 0x4E00 for ch in s):
                    add_text_cps(cps, s)
    return cps


def gb2312_codepoints() -> set[int]:
    cps: set[int] = set()
    for qu in range(0xA1, 0xF8):
        for wei in range(0xA1, 0xFF):
            try:
                ch = bytes((qu, wei)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            for c in ch:
                cp = ord(c)
                if cp >= 0x80:
                    cps.add(cp)
    return cps


def load_font(size: int) -> ImageFont.FreeTypeFont:
    last_err: Exception | None = None
    for path in FONT_CANDIDATES:
        p = Path(path)
        if not p.is_file():
            continue
        for idx in (2, 0, 3, 1, 4):
            try:
                return ImageFont.truetype(str(p), size=size, index=idx)
            except OSError as e:
                last_err = e
                continue
    raise SystemExit(f"Noto Sans CJK not found: {last_err}")


def glyph_bits_1bpp(font: ImageFont.FreeTypeFont, cp: int, dim: int, thresh: int) -> list[int]:
    ch = chr(cp)
    bpr = (dim + 7) // 8
    img = Image.new("L", (dim, dim), 0)
    draw = ImageDraw.Draw(img)
    try:
        bbox = draw.textbbox((0, 0), ch, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        x = (dim - tw) // 2 - bbox[0]
        y = (dim - th) // 2 - bbox[1]
    except Exception:
        x, y = 0, 0
    draw.text((x, y), ch, font=font, fill=255)
    pix = img.load()
    rows: list[int] = []
    for row in range(dim):
        for byte_i in range(bpr):
            b = 0
            for bit in range(8):
                col = byte_i * 8 + bit
                if col < dim and pix[col, row] >= thresh:
                    b |= 0x80 >> bit
            rows.append(b)
    if all(v == 0 for v in rows):
        for col in range(dim):
            rows[0 * bpr + col // 8] |= 0x80 >> (col % 8)
            rows[(dim - 1) * bpr + col // 8] |= 0x80 >> (col % 8)
        for row in range(dim):
            rows[row * bpr] |= 0x80
            rows[row * bpr + bpr - 1] |= 0x01
    return rows


def glyph_bits_4bpp(font: ImageFont.FreeTypeFont, cp: int, dim: int) -> list[int]:
    """每像素 4bit；一行 dim/2 字节；高半字节=偶数列。

    疏密字 Noto 栅格峰值/骨干亮度差大 → 按字 ink 参考对齐：
    骨干（≥ref·body）一律 15，仅边缘走灰，避免「有的黑有的灰」。
    """
    ch = chr(cp)
    bpr = (dim + 1) // 2
    img = Image.new("L", (dim, dim), 0)
    draw = ImageDraw.Draw(img)
    try:
        bbox = draw.textbbox((0, 0), ch, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        x = (dim - tw) // 2 - bbox[0]
        y = (dim - th) // 2 - bbox[1]
    except Exception:
        x, y = 0, 0
    draw.text((x, y), ch, font=font, fill=255)
    pix = img.load()

    ink: list[int] = []
    peak = 0
    for row in range(dim):
        for col in range(dim):
            v = int(pix[col, row])
            if v > peak:
                peak = v
            if v >= 28:
                ink.append(v)
    if peak < 8:
        peak = 255
    # 参考 = ink 的 p55（笔画主体，勿用 peak/p80——空心字主体偏灰会被高百分位抬飞）
    if ink:
        ink.sort()
        ref = ink[int((len(ink) - 1) * 0.55)]
        if ref < 40:
            ref = max(peak * 2 // 3, 40)
    else:
        ref = peak
    if ref < 1:
        ref = 1

    floor_v = max(14, ref // 14)
    body_t = 0.22  # ≥ 此相对值 → 实黑；灰只留给最淡 AA

    def lev(col: int, row: int) -> int:
        v = int(pix[col, row])
        if v < floor_v:
            return 0
        t = v / float(ref)
        if t > 1.0:
            t = 1.0
        if t >= body_t:
            return 15
        # 边缘：0..body → 1..14
        n = int(t / body_t * 14.0 + 0.5)
        if n < 1:
            return 1
        return 14 if n > 14 else n

    rows: list[int] = []
    for row in range(dim):
        for byte_i in range(bpr):
            c0 = byte_i * 2
            c1 = c0 + 1
            hi = lev(c0, row) if c0 < dim else 0
            lo = lev(c1, row) if c1 < dim else 0
            rows.append((hi << 4) | lo)
    if all(v == 0 for v in rows):
        for col in range(dim):
            bi = col // 2
            if (col & 1) == 0:
                rows[0 * bpr + bi] |= 0xF0
                rows[(dim - 1) * bpr + bi] |= 0xF0
            else:
                rows[0 * bpr + bi] |= 0x0F
                rows[(dim - 1) * bpr + bi] |= 0x0F
        for row in range(dim):
            rows[row * bpr] |= 0xF0
            rows[row * bpr + bpr - 1] |= 0x0F
    return rows


def emit_c(
    cps: list[int],
    bits: dict[int, list[int]],
    out: Path,
    dim: int,
    bpp: int,
) -> None:
    if bpp == 4:
        bpr = (dim + 1) // 2
    else:
        bpr = (dim + 7) // 8
    bpg = dim * bpr
    lines: list[str] = []
    lines.append("/*")
    lines.append(f" * {out.name} — {dim}×{dim}×{bpp}bpp 汉字（PR-UI-cjk-gray；GB2312）")
    lines.append(" * Tools/Scripts/gen-cjk32.py · Noto SC Medium；骨干实黑+边缘灰，色度对齐。")
    if bpp == 4:
        lines.append(" * 4bpp：每字节两像素（高半字节=偶数列）；alpha=nibble*17。")
    lines.append(f" * 字形数 {len(cps)}。重跑：python3 Tools/Scripts/gen-cjk32.py --bpp {bpp}")
    lines.append(" */")
    lines.append('#include "Font.h"')
    lines.append("")
    lines.append(f"#define CJK32_COUNT {len(cps)}")
    lines.append(f"#define CJK32_DIM {dim}")
    lines.append(f"#define CJK32_BPP {bpp}")
    lines.append("")
    lines.append(f"static const UINT8 gCjk32Bits[CJK32_COUNT][{bpg}] = {{")
    for cp in cps:
        hexes = ", ".join(f"0x{v:02X}" for v in bits[cp])
        lines.append(f"    {{ {hexes} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const UINT32 gCjk32Cp[CJK32_COUNT] = {")
    for i in range(0, len(cps), 16):
        chunk = ", ".join(f"0x{c:04X}" for c in cps[i : i + 16])
        comma = "," if i + 16 < len(cps) else ""
        lines.append(f"    {chunk}{comma}")
    lines.append("};")
    lines.append("")
    lines.append("UINT32 FontCjkBitsPerPixel(void) {")
    lines.append("    return CJK32_BPP;")
    lines.append("}")
    lines.append("")
    lines.append("UINT32 FontCjkDim(void) {")
    lines.append("    return CJK32_DIM;")
    lines.append("}")
    lines.append("")
    lines.append("const UINT8 *FontCjk32Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {")
    lines.append("    UINT32 Lo = 0;")
    lines.append("    UINT32 Hi = CJK32_COUNT;")
    lines.append("    while (Lo < Hi) {")
    lines.append("        UINT32 Mid = Lo + (Hi - Lo) / 2;")
    lines.append("        UINT32 V = gCjk32Cp[Mid];")
    lines.append("        if (V == Cp) {")
    lines.append("            if (OutW) { *OutW = CJK32_DIM; }")
    lines.append("            if (OutH) { *OutH = CJK32_DIM; }")
    lines.append("            return gCjk32Bits[Mid];")
    lines.append("        }")
    lines.append("        if (V < Cp) { Lo = Mid + 1; } else { Hi = Mid; }")
    lines.append("    }")
    lines.append("    return 0;")
    lines.append("}")
    lines.append("")
    lines.append("const UINT8 *FontCjk16Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {")
    lines.append("    return FontCjk32Lookup(Cp, OutW, OutH);")
    lines.append("}")
    lines.append("")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate CJK bitmap (default 18×18×4bpp GB2312)")
    ap.add_argument("--dim", type=int, default=18, choices=(16, 18, 20, 24, 28, 32))
    ap.add_argument("--bpp", type=int, default=4, choices=(1, 4))
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--size", type=int, default=0, help="Noto size; 0=auto")
    ap.add_argument("--thresh", type=int, default=0, help="1bpp only; 0=auto")
    ap.add_argument("--set", choices=("gb2312", "ui"), default="gb2312")
    args = ap.parse_args()

    dim = args.dim
    bpp = args.bpp
    out = args.out or (ROOT / "CodeB-Library/Fonts/cjk32.c")
    size = args.size or {16: 14, 18: 15, 20: 17, 24: 20, 28: 24, 32: 27}[dim]
    thresh = args.thresh or {16: 120, 18: 118, 20: 112, 24: 112, 28: 100, 32: 96}[dim]

    loc = zh_locale_codepoints()
    src = source_string_codepoints()
    vocab: set[int] = set()
    add_text_cps(vocab, COMPUTER_VOCAB)
    if args.set == "gb2312":
        base = gb2312_codepoints()
        union = base | loc | src | vocab
        tag = f"gb2312={len(base)}"
    else:
        union = loc | src | vocab
        tag = "ui-only"
    cps = sorted(union)
    font = load_font(size)
    bits: dict[int, list[int]] = {}
    n = len(cps)
    for i, cp in enumerate(cps):
        if bpp == 4:
            bits[cp] = glyph_bits_4bpp(font, cp, dim)
        else:
            bits[cp] = glyph_bits_1bpp(font, cp, dim, thresh)
        if (i + 1) % 500 == 0 or i + 1 == n:
            print(f"  raster {i + 1}/{n}", file=sys.stderr)
    emit_c(cps, bits, out, dim, bpp)
    bpr = (dim + 1) // 2 if bpp == 4 else (dim + 7) // 8
    print(
        f"wrote {out} count={n} dim={dim} bpp={bpp} ({tag}) "
        f"font_size={size} glyph_bytes≈{n * dim * bpr / 1024:.0f}KiB",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
