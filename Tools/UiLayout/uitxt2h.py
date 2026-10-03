#!/usr/bin/env python3
# uitxt2h.py — .uitxt → 编译期 C 头（标准库 only）
# 用法: python3 uitxt2h.py MyApp.uitxt > MyAppUi.h
from __future__ import print_function

import os
import re
import sys

KINDS = {
    "label": "TOY_UI_WIDGET_LABEL",
    "button": "TOY_UI_WIDGET_BUTTON",
    "checkbox": "TOY_UI_WIDGET_CHECKBOX",
    "textbox": "TOY_UI_WIDGET_TEXTBOX",
}

RE_WIN = re.compile(r'^window\s+"([^"]*)"\s+(\d+)\s+(\d+)\s*$')
RE_KV = re.compile(
    r'([a-z]+)=(?:"([^"]*)"|(\S+))'
)
RE_ID = re.compile(r'^[a-z][a-z0-9]*$')
RE_TYPE = re.compile(
    r'^(label|button|checkbox|textbox)\s+(.+)$'
)


def die(msg):
    sys.stderr.write("uitxt2h: %s\n" % msg)
    sys.exit(1)


def parse_widget(lineno, rest):
    fields = {}
    for m in RE_KV.finditer(rest):
        key = m.group(1)
        val = m.group(2) if m.group(2) is not None else m.group(3)
        fields[key] = val
    for req in ("id", "x", "y", "w", "h", "text"):
        if req not in fields:
            die("line %d: missing %s" % (lineno, req))
    wid = fields["id"]
    if not RE_ID.match(wid):
        die("line %d: bad id %r" % (lineno, wid))
    try:
        x = int(fields["x"])
        y = int(fields["y"])
        w = int(fields["w"])
        h = int(fields["h"])
    except ValueError:
        die("line %d: x/y/w/h must be integers" % lineno)
    return wid, x, y, w, h, fields["text"]


def main():
    if len(sys.argv) != 2:
        die("usage: uitxt2h.py <file.uitxt>")
    path = sys.argv[1]
    if not os.path.isfile(path):
        die("file not found: %s" % path)

    base = os.path.splitext(os.path.basename(path))[0]
    if not base:
        die("bad filename")
    prefix = re.sub(r'[^A-Za-z0-9]', '', base).upper()
    if not prefix or prefix[0].isdigit():
        prefix = "APP" + prefix
    guard = prefix + "_UI_H"
    arr = "g" + base[0].upper() + base[1:] + "Widgets"
    if not base[1:]:
        arr = "g" + base.upper() + "Widgets"

    title = None
    win_w = win_h = None
    widgets = []
    seen = {}

    with open(path, "r") as f:
        lines = f.readlines()

    first = True
    for lineno, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if first:
            first = False
            m = RE_WIN.match(line)
            if not m:
                die("line %d: first entry must be window \"title\" W H" % lineno)
            title, win_w, win_h = m.group(1), int(m.group(2)), int(m.group(3))
            continue
        m = RE_TYPE.match(line)
        if not m:
            die("line %d: expected label|button|checkbox|textbox" % lineno)
        kind, rest = m.group(1), m.group(2)
        wid, x, y, w, h, text = parse_widget(lineno, rest)
        if wid in seen:
            die("line %d: duplicate id %r (also line %d)" % (
                lineno, wid, seen[wid]))
        seen[wid] = lineno
        widgets.append((KINDS[kind], wid, x, y, w, h, text))

    if title is None:
        die("empty file: need window line")

    out = []
    a = out.append
    a("/* 自动生成，勿手改 */")
    a("#ifndef %s" % guard)
    a("#define %s" % guard)
    a("")
    a('#include "ToyUiLayout.h"')
    a("")
    a("#define %s_WIN_W %d" % (prefix, win_w))
    a("#define %s_WIN_H %d" % (prefix, win_h))
    a("#define %s_WIN_TITLE \"%s\"" % (prefix, title.replace("\\", "\\\\").replace("\"", "\\\"")))
    a("")
    a("enum {")
    for i, w in enumerate(widgets):
        enum_name = "ID_" + w[1].upper()
        comma = "," if i + 1 < len(widgets) else ""
        a("    %-10s = %d%s" % (enum_name, i, comma))
    if not widgets:
        a("    ID_NONE = 0")
    a("};")
    a("")
    a("static const TOY_UI_WIDGET %s[] = {" % arr)
    for i, w in enumerate(widgets):
        kind, wid, x, y, ww, hh, text = w
        etext = text.replace("\\", "\\\\").replace("\"", "\\\"")
        a("    { %s, ID_%s, %d, %d, %d, %d, \"%s\" }," % (
            kind, wid.upper(), x, y, ww, hh, etext))
    a("};")
    a("")
    a("#define %s_WIDGET_COUNT %d" % (prefix, len(widgets)))
    a("")
    a("#endif")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
