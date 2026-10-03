#!/usr/bin/env python3
# gen-cjk-ttf-subset.py — 切真 CJK.TTF 子集（stb 只认 TrueType glyf，禁 CFF）
# 依赖宿主：fonttools + 含 glyf 的 CJK 源（优先 DroidSansFallbackFull / Noto SC TTF）
"""Usage:
  python3 Tools/Scripts/gen-cjk-ttf-subset.py           # UI 汉字（默认）
  python3 Tools/Scripts/gen-cjk-ttf-subset.py --set cjk32
  python3 Tools/Scripts/gen-cjk-ttf-subset.py --set gb2312
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
import tempfile
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTCollection, TTFont

ROOT = Path(__file__).resolve().parents[2]
SEED = ROOT / "Tools" / "Fonts" / "CJK.TTF"
IMAGE = ROOT.parent / "ToyImage" / "Assets" / "Fonts" / "CJK.TTF"
CJK32_C = ROOT / "CodeB-Library" / "Fonts" / "cjk32.c"
GEN32 = Path(__file__).resolve().parent / "gen-cjk32.py"

# stb_truetype 只解析 glyf。NotoSansCJK*.ttc 是 CFF → 进镜像等于 stub。
FONT_CANDIDATES = [
    # TrueType（优先）
    ROOT / "Tools" / "Fonts" / "src" / "NotoSansSC-VF.ttf",
    ROOT / "Tools" / "Fonts" / "src" / "NotoSansSC[wght].ttf",
    Path("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"),
    Path("/usr/share/fonts/truetype/droid/DroidSansFallback.ttf"),
    # 仅当本机已下 TrueType 版 Noto；CFF TTC 在 pick 时跳过
    Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc"),
]


def load_gen32():
    spec = importlib.util.spec_from_file_location("gen_cjk32", GEN32)
    if not spec or not spec.loader:
        raise SystemExit(f"cannot load {GEN32}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse_cjk32_cps() -> set[int]:
    text = CJK32_C.read_text(encoding="utf-8")
    m = re.search(r"gCjk32Cp\[CJK32_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("gCjk32Cp not found in cjk32.c")
    out: set[int] = set()
    for tok in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1)):
        cp = int(tok, 16)
        if cp >= 0x80:
            out.add(cp)
    return out


def font_is_glyf(path: Path, face: int) -> bool:
    try:
        if path.suffix.lower() == ".ttc":
            col = TTCollection(str(path))
            if face >= len(col.fonts):
                face = 0
            font = col.fonts[face]
        else:
            font = TTFont(str(path))
        return "glyf" in font and "CFF " not in font
    except Exception:
        return False


def pick_source(face_hint: int) -> tuple[Path, int]:
    for path in FONT_CANDIDATES:
        p = Path(path)
        if not p.is_file():
            continue
        face = 0 if p.suffix.lower() != ".ttc" else (2 if face_hint < 0 else face_hint)
        if font_is_glyf(p, face):
            return p, face
        print(f"skip CFF/non-glyf: {p}", file=sys.stderr)
    raise SystemExit(
        "no TrueType (glyf) CJK source found.\n"
        "  install fonts-droid-fallback, or put NotoSansSC[wght].ttf under "
        "Tools/Fonts/src/\n"
        "  (NotoSansCJK*.ttc is CFF — stb cannot rasterize)"
    )


def collect(set_name: str, g32) -> set[int]:
    loc = g32.zh_locale_codepoints()
    src = g32.source_string_codepoints()
    vocab: set[int] = set()
    g32.add_text_cps(vocab, g32.COMPUTER_VOCAB)
    if set_name == "ui":
        return loc | src | vocab
    if set_name == "cjk32":
        return parse_cjk32_cps() | loc
    if set_name == "gb2312":
        return g32.gb2312_codepoints() | loc | src | vocab
    raise SystemExit(f"unknown set {set_name}")


def extract_face(ttc: Path, index: int, out_ttf: Path) -> None:
    col = TTCollection(str(ttc))
    if index >= len(col.fonts):
        index = 0
    font: TTFont = col.fonts[index]
    font.flavor = None
    font.save(str(out_ttf))


def run_subset(src_ttf: Path, unicodes: set[int], dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    uni_txt = dest.with_suffix(".unicodes.txt")
    uni_txt.write_text(
        "\n".join(f"U+{cp:04X}" for cp in sorted(unicodes)) + "\n",
        encoding="utf-8",
    )
    args = [
        str(src_ttf),
        f"--unicodes-file={uni_txt}",
        f"--output-file={dest}",
        "--layout-features=",
        "--no-layout-closure",
        "--glyph-names",
        "--symbol-cmap",
        "--legacy-cmap",
        "--notdef-glyph",
        "--notdef-outline",
        "--recommended-glyphs",
        "--name-IDs=*",
        "--name-legacy",
        "--name-languages=*",
        "--drop-tables+=FFTM,GPOS,GSUB,GDEF,BASE,VORG,vhea,vmtx",
    ]
    subset.main(args)
    try:
        uni_txt.unlink()
    except OSError:
        pass


def assert_glyf_usable(path: Path, sample_cps: set[int]) -> None:
    font = TTFont(str(path))
    if "glyf" not in font:
        raise SystemExit(f"output missing glyf (CFF?): {path}")
    if "CFF " in font:
        raise SystemExit(f"output still has CFF — stb cannot use it: {path}")
    cmap = font.getBestCmap() or {}
    glyf = font["glyf"]
    ok = 0
    for cp in sorted(sample_cps)[:32]:
        name = cmap.get(cp)
        if not name or name not in glyf:
            continue
        g = glyf[name]
        if getattr(g, "numberOfContours", 0) != 0:
            ok += 1
    if ok == 0:
        raise SystemExit(f"output glyf has no outlines for sample UI cps: {path}")
    print(f"glyf ok sample_hits={ok}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build real Assets/Fonts/CJK.TTF subset (glyf)")
    ap.add_argument("--set", choices=("ui", "cjk32", "gb2312"), default="ui")
    ap.add_argument("--font", type=Path, default=None, help="source .ttc/.ttf (must be glyf)")
    ap.add_argument("--face", type=int, default=-1, help="TTC face index (−1=SC≈2)")
    args = ap.parse_args()

    g32 = load_gen32()
    unicodes = collect(args.set, g32)
    if not unicodes:
        raise SystemExit("empty unicode set")

    if args.font:
        src = args.font
        face = 0 if args.face < 0 else args.face
        if not font_is_glyf(src, face):
            raise SystemExit(f"source is not TrueType glyf: {src}")
    else:
        src, face = pick_source(args.face)

    with tempfile.TemporaryDirectory(prefix="toy-cjk-ttf-") as td:
        tmp = Path(td)
        face_ttf = tmp / "face.ttf"
        if src.suffix.lower() == ".ttc":
            print(f"extract face={face} from {src}", file=sys.stderr)
            extract_face(src, face, face_ttf)
            if not font_is_glyf(face_ttf, 0):
                raise SystemExit("extracted face is CFF — use a TrueType source")
        else:
            face_ttf = src
        out_tmp = tmp / "CJK.TTF"
        run_subset(face_ttf, unicodes, out_tmp)
        assert_glyf_usable(out_tmp, unicodes)
        data = out_tmp.read_bytes()

    for dest in (SEED, IMAGE):
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        print(f"wrote {dest} ({len(data)} bytes)")

    print(
        f"set={args.set} cps={len(unicodes)} src={src} face={face}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
