#!/usr/bin/env python3
# gen-cjk-ttf-stub.py — 最小合法 TTF（仅 .notdef），供 PR-UI-ttf-0 探 sfnt。
# 宿主无 fontTools 时也能生成；真正 CJK 子集留给有 fonttools 再跑。
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEED = ROOT / "Tools" / "Fonts" / "CJK.TTF"
IMAGE = ROOT.parent / "ToyImage" / "Assets" / "Fonts" / "CJK.TTF"


def u16(v: int) -> bytes:
    return struct.pack(">H", v & 0xFFFF)


def u32(v: int) -> bytes:
    return struct.pack(">I", v & 0xFFFFFFFF)


def i16(v: int) -> bytes:
    return struct.pack(">h", v)


def align4(b: bytes) -> bytes:
    return b + b"\x00" * ((4 - len(b) % 4) % 4)


def checksum(data: bytes) -> int:
    data = align4(data)
    s = 0
    for i in range(0, len(data), 4):
        s = (s + struct.unpack(">I", data[i : i + 4])[0]) & 0xFFFFFFFF
    return s


def maxp_tbl() -> bytes:
    # version 1.0, 1 glyph, rest zero
    return u32(0x00010000) + u16(1) + b"\x00" * 26


def head_tbl() -> bytes:
    # 54 bytes; checkSumAdjustment filled later
    return b"".join(
        [
            u32(0x00010000),
            u32(0x00010000),
            u32(0),  # checkSumAdjustment
            u32(0x5F0F3CF5),
            u16(0),  # flags
            u16(16),  # unitsPerEm
            b"\x00" * 16,  # created/modified
            i16(0),
            i16(0),
            i16(0),
            i16(0),  # x/y min/max
            u16(0),
            u16(0),
            i16(2),  # indexToLocFormat = 1 (long)
            i16(0),
        ]
    )


def hhea_tbl() -> bytes:
    # 36 bytes: version + 14×i16 + numberOfHMetrics
    return b"".join(
        [
            u32(0x00010000),
            i16(0),
            i16(0),
            i16(0),
            u16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            i16(0),
            u16(1),
        ]
    )


def hmtx_tbl() -> bytes:
    return u16(0) + i16(0)


def loca_tbl() -> bytes:
    return u32(0) + u32(0)


def name_tbl() -> bytes:
    s = b"ToyOS CJK stub"
    rec = u16(0) + u16(3) + u16(0) + u16(4) + u16(len(s)) + u16(0)
    return u16(0) + u16(1) + u16(6 + 12) + rec + s


def post_tbl() -> bytes:
    return u32(0x00030000) + b"\x00" * 28


def cmap_tbl() -> bytes:
    # format 4, empty segments (just 0xFFFF)
    fmt4 = u16(4) + u16(24) + u16(0) + u16(2) + u16(2) + u16(0) + u16(0)
    fmt4 += u16(0xFFFF) + u16(0) + u16(0xFFFF) + i16(1) + u16(0)
    header = u16(0) + u16(1) + u16(3) + u16(1) + u32(12)
    return header + fmt4


def assemble(tables: dict[bytes, bytes]) -> bytes:
    tags = sorted(tables.keys())
    n = len(tags)
    es = 0
    sr = 1
    while sr * 2 <= n:
        sr *= 2
        es += 1
    search_range = sr * 16
    range_shift = (n - sr) * 16
    offset = 12 + 16 * n
    recs = []
    blobs = []
    for tag in tags:
        raw = tables[tag]
        pad = align4(raw)
        recs.append(tag + u32(checksum(raw)) + u32(offset) + u32(len(raw)))
        blobs.append(pad)
        offset += len(pad)
    header = u32(0x00010000) + u16(n) + u16(search_range) + u16(es) + u16(range_shift)
    font = header + b"".join(recs) + b"".join(blobs)
    total = checksum(font)
    adj = (0xB1B0AFBA - total) & 0xFFFFFFFF
    head_off = 12 + 16 * n
    for i, tag in enumerate(tags):
        if tag == b"head":
            head_off = 12 + 16 * n + sum(len(align4(tables[t])) for t in tags[:i])
            break
    return font[: head_off + 8] + u32(adj) + font[head_off + 12 :]


def main() -> None:
    tables = {
        b"cmap": cmap_tbl(),
        b"glyf": b"",
        b"head": head_tbl(),
        b"hhea": hhea_tbl(),
        b"hmtx": hmtx_tbl(),
        b"loca": loca_tbl(),
        b"maxp": maxp_tbl(),
        b"name": name_tbl(),
        b"post": post_tbl(),
    }
    data = assemble(tables)
    for dest in (SEED, IMAGE):
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        print(f"wrote {dest} ({len(data)} bytes, tables={len(tables)})")


if __name__ == "__main__":
    main()
