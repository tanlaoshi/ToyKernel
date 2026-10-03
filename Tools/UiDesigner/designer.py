#!/usr/bin/env python3
# ToyOS UI Designer — PC 端拖控件 → .uitxt / .h（标准库 only）
# 用法: python3 Tools/UiDesigner/designer.py
# 依赖: Python3 + Tkinter（debian: python3-tk）
from __future__ import print_function

import copy
import os
import re
import sys

DESIGN_W = 1280
DESIGN_H = 720
GRID = 40
DEFAULT_W, DEFAULT_H = 80, 28
HANDLE = 8
CANVAS_DISP_W = 900  # 画布初始显示尺寸（会随窗口缩放）
CANVAS_DISP_H = 506

KINDS = ("button", "label", "checkbox", "textbox")
KIND_LABEL = {
    "button": "Button",
    "label": "Label",
    "checkbox": "CheckBox",
    "textbox": "TextBox",
}
KIND_PREFIX = {
    "button": "btn",
    "label": "lbl",
    "checkbox": "chk",
    "textbox": "txt",
}


def usage():
    print("ToyOS UI Designer")
    print("  python3 Tools/UiDesigner/designer.py")
    print("  python3 Tools/UiDesigner/designer.py --help")
    print("Requires: Python 3 + Tkinter (e.g. apt install python3-tk)")
    print("Export: .uitxt (see Documents/开发/布局文件格式.md) then uitxt2h → .h")


def script_dir():
    return os.path.dirname(os.path.abspath(__file__))


def toykernel_root():
    return os.path.abspath(os.path.join(script_dir(), "..", ".."))


def uitxt2h_path():
    return os.path.join(toykernel_root(), "Tools", "UiLayout", "uitxt2h.py")


class DesignerApp(object):
    def __init__(self, tk, ttk):
        self.tk = tk
        self.widgets = []  # dict kind,id,text,x,y,w,h
        self.selected = None
        self.place_kind = None
        self.drag = None  # (mode, ox, oy, sx, sy, sw, sh)
        self.path = None
        self.title = "MyApp"
        self._counters = {k: 0 for k in KINDS}
        self._prop_lock = False
        self.sx = float(CANVAS_DISP_W) / DESIGN_W
        self.sy = float(CANVAS_DISP_H) / DESIGN_H
        self.offx = 0.0
        self.offy = 0.0
        self.canvas_w = CANVAS_DISP_W
        self.canvas_h = CANVAS_DISP_H
        self.history = []      # 撤销栈：snapshot = (widgets, selected, counters, title, path)
        self.future = []       # 重做栈
        self.HIST_MAX = 100
        self._drag_dirty = False  # move/resize 是否已产生实际位移（用于延迟入栈）

        self.root = tk.Tk()
        self.root.title("ToyOS UI Designer")
        self.root.geometry("1500x860")
        self._build_menu()
        self._build_body()
        self._build_status()
        self._bind_keys()
        self.refresh()

    def _build_menu(self):
        tk = self.tk
        m = tk.Menu(self.root)
        self.root.config(menu=m)
        mf = tk.Menu(m, tearoff=0)
        m.add_cascade(label="文件", menu=mf)
        mf.add_command(label="新建", command=self.file_new)
        mf.add_command(label="打开 .uitxt…", command=self.file_open)
        mf.add_command(label="保存 .uitxt…", command=self.file_save)
        mf.add_separator()
        mf.add_command(label="退出", command=self.root.quit)
        me = tk.Menu(m, tearoff=0)
        m.add_cascade(label="编辑", menu=me)
        me.add_command(label="撤销", command=self.undo, accelerator="Ctrl+Z")
        me.add_command(label="重做", command=self.redo, accelerator="Ctrl+Y")
        me.add_separator()
        me.add_command(label="删除选中", command=self.delete_selected)
        me.add_command(label="清空", command=self.clear_all)
        mx = tk.Menu(m, tearoff=0)
        m.add_cascade(label="导出", menu=mx)
        mx.add_command(label="导出 .uitxt…", command=self.export_uitxt)
        mx.add_command(label="导出 .h…", command=self.export_h)

    def _build_body(self):
        tk = self.tk
        body = tk.Frame(self.root)
        body.pack(fill=tk.BOTH, expand=True)

        left = tk.Frame(body, width=180)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=4, pady=4)
        left.pack_propagate(False)
        tk.Label(left, text="控件面板").pack(pady=(0, 4))
        for k in KINDS:
            tk.Button(
                left, text=KIND_LABEL[k],
                command=lambda kk=k: self.set_place(kk)
            ).pack(pady=2, fill=tk.X, padx=4)

        mid = tk.Frame(body)
        mid.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4, pady=4)
        tk.Label(mid, text="画布 %d×%d（随窗口缩放，逻辑尺寸不变）" % (DESIGN_W, DESIGN_H)).pack()
        self.canvas = tk.Canvas(
            mid, width=CANVAS_DISP_W, height=CANVAS_DISP_H,
            bg="#f4f4f4", highlightthickness=1, highlightbackground="#888"
        )
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<Button-1>", self.on_down)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_up)
        self.canvas.bind("<Configure>", self.on_canvas_resize)

        right = tk.Frame(body, width=260)
        right.pack(side=tk.RIGHT, fill=tk.Y, padx=4, pady=4)
        right.pack_propagate(False)
        tk.Label(right, text="属性栏").pack(pady=(0, 4))
        self.ents = {}
        for key in ("id", "text", "x", "y", "w", "h"):
            row = tk.Frame(right)
            row.pack(fill=tk.X, pady=2, padx=4)
            tk.Label(row, text=key + ":", width=5, anchor="w").pack(side=tk.LEFT)
            e = tk.Entry(row, width=12)
            e.pack(side=tk.LEFT, fill=tk.X, expand=True)
            e.bind("<KeyRelease>", self.on_prop)
            e.bind("<FocusOut>", self.on_prop)
            e.bind("<FocusIn>", self.on_prop_focus_in)
            self.ents[key] = e
        self._set_props_enabled(False)

    def _build_status(self):
        self.status = self.tk.Label(
            self.root, text="0 个控件", anchor="w", relief="sunken"
        )
        self.status.pack(fill=self.tk.X, side=self.tk.BOTTOM)

    def _bind_keys(self):
        self.root.bind("<Delete>", lambda e: self.delete_selected())
        self.root.bind("<Left>", lambda e: self.nudge(-1, 0, e))
        self.root.bind("<Right>", lambda e: self.nudge(1, 0, e))
        self.root.bind("<Up>", lambda e: self.nudge(0, -1, e))
        self.root.bind("<Down>", lambda e: self.nudge(0, 1, e))
        self.root.bind("<Control-z>", lambda e: self.undo())
        self.root.bind("<Control-y>", lambda e: self.redo())
        self.root.bind("<Control-Z>", lambda e: self.redo())  # Ctrl+Shift+Z = 重做

    def set_place(self, kind):
        self.place_kind = kind
        self.status.config(text="待放置: %s" % KIND_LABEL[kind])

    def next_id(self, kind):
        self._counters[kind] += 1
        return "%s%d" % (KIND_PREFIX[kind], self._counters[kind])

    def sync_counters(self):
        self._counters = {k: 0 for k in KINDS}
        for w in self.widgets:
            m = re.match(r"^(btn|lbl|chk|txt)(\d+)$", w["id"])
            if not m:
                continue
            pref, n = m.group(1), int(m.group(2))
            kind = {v: k for k, v in KIND_PREFIX.items()}[pref]
            if n > self._counters[kind]:
                self._counters[kind] = n

    # ---- 撤销 / 重做（快照式：每次突变前压栈当前完整状态） ----
    def _snapshot(self):
        return (
            copy.deepcopy(self.widgets), self.selected,
            dict(self._counters), self.title, self.path,
        )

    def _restore(self, snap):
        ws, sel, cnt, title, path = snap
        self.widgets = copy.deepcopy(ws)
        self.selected = sel
        self._counters = dict(cnt)
        self.title = title
        self.path = path

    def _push_undo(self):
        snap = self._snapshot()
        if self.history and self.history[-1] == snap:
            return  # 状态未变，跳过（去重：空点击 / 重复 FocusIn）
        self.history.append(snap)
        if len(self.history) > self.HIST_MAX:
            self.history.pop(0)
        self.future = []  # 新突变清空重做栈

    def undo(self):
        if not self.history:
            return
        self.future.append(self._snapshot())
        self._restore(self.history.pop())
        self.refresh()

    def redo(self):
        if not self.future:
            return
        self.history.append(self._snapshot())
        self._restore(self.future.pop())
        self.refresh()

    def on_prop_focus_in(self, _ev=None):
        # 进入属性编辑前压栈；若未改值则 _push_undo 自身去重
        self._push_undo()

    def on_canvas_resize(self, ev):
        # 画布随窗口缩放：uniform scale 保 16:9，居中
        self.canvas_w = max(1, ev.width)
        self.canvas_h = max(1, ev.height)
        s = min(float(self.canvas_w) / DESIGN_W, float(self.canvas_h) / DESIGN_H)
        self.sx = self.sy = s
        self.offx = (self.canvas_w - DESIGN_W * s) / 2.0
        self.offy = (self.canvas_h - DESIGN_H * s) / 2.0
        self.draw()

    def lx(self, cx):
        return int((cx - self.offx) / self.sx)

    def ly(self, cy):
        return int((cy - self.offy) / self.sy)

    def cx(self, x):
        return self.offx + x * self.sx

    def cy(self, y):
        return self.offy + y * self.sy

    def hit(self, lx, ly):
        for i in range(len(self.widgets) - 1, -1, -1):
            w = self.widgets[i]
            if w["x"] <= lx < w["x"] + w["w"] and w["y"] <= ly < w["y"] + w["h"]:
                return i
        return None

    def in_handle(self, w, lx, ly):
        return (w["x"] + w["w"] - HANDLE <= lx <= w["x"] + w["w"] and
                w["y"] + w["h"] - HANDLE <= ly <= w["y"] + w["h"])

    def on_down(self, ev):
        lx, ly = self.lx(ev.x), self.ly(ev.y)
        if self.place_kind:
            self._push_undo()
            w = {
                "kind": self.place_kind,
                "id": self.next_id(self.place_kind),
                "text": KIND_LABEL[self.place_kind],
                "x": max(0, min(lx, DESIGN_W - DEFAULT_W)),
                "y": max(0, min(ly, DESIGN_H - DEFAULT_H)),
                "w": DEFAULT_W,
                "h": DEFAULT_H,
            }
            self.widgets.append(w)
            self.selected = len(self.widgets) - 1
            self.place_kind = None
            self.refresh()
            return
        idx = self.hit(lx, ly)
        self.selected = idx
        if idx is None:
            self.drag = None
            self.refresh()
            return
        w = self.widgets[idx]
        mode = "resize" if self.in_handle(w, lx, ly) else "move"
        self.drag = (mode, lx, ly, w["x"], w["y"], w["w"], w["h"])
        self._drag_dirty = False
        self.refresh()

    def on_drag(self, ev):
        if self.drag is None or self.selected is None:
            return
        if not self._drag_dirty:
            self._push_undo()
            self._drag_dirty = True
        mode, ox, oy, sx, sy, sw, sh = self.drag
        lx, ly = self.lx(ev.x), self.ly(ev.y)
        w = self.widgets[self.selected]
        if mode == "move":
            w["x"] = max(0, min(sx + (lx - ox), DESIGN_W - w["w"]))
            w["y"] = max(0, min(sy + (ly - oy), DESIGN_H - w["h"]))
        else:
            w["w"] = max(8, min(sw + (lx - ox), DESIGN_W - w["x"]))
            w["h"] = max(8, min(sh + (ly - oy), DESIGN_H - w["y"]))
        self.refresh()

    def on_up(self, _ev):
        self.drag = None
        self._drag_dirty = False

    def nudge(self, dx, dy, ev):
        if self.selected is None:
            return
        self._push_undo()
        step = 10 if (getattr(ev, "state", 0) & 0x0001) else 1
        w = self.widgets[self.selected]
        w["x"] = max(0, min(w["x"] + dx * step, DESIGN_W - w["w"]))
        w["y"] = max(0, min(w["y"] + dy * step, DESIGN_H - w["h"]))
        self.refresh()

    def delete_selected(self):
        if self.selected is None:
            return
        self._push_undo()
        del self.widgets[self.selected]
        self.selected = None
        self.refresh()

    def clear_all(self):
        self._push_undo()
        self.widgets = []
        self.selected = None
        self._counters = {k: 0 for k in KINDS}
        self.refresh()

    def file_new(self):
        self.clear_all()
        self.path = None
        self.title = "MyApp"

    def file_open(self):
        from tkinter import filedialog
        p = filedialog.askopenfilename(filetypes=[("uitxt", "*.uitxt"), ("all", "*.*")])
        if not p:
            return
        self.load_uitxt(p)

    def file_save(self):
        from tkinter import filedialog
        p = self.path or filedialog.asksaveasfilename(
            defaultextension=".uitxt", filetypes=[("uitxt", "*.uitxt")]
        )
        if not p:
            return
        self.path = p
        self.write_uitxt(p)

    def export_uitxt(self):
        from tkinter import filedialog
        p = filedialog.asksaveasfilename(
            defaultextension=".uitxt", filetypes=[("uitxt", "*.uitxt")]
        )
        if p:
            self.write_uitxt(p)

    def export_h(self):
        import subprocess
        import tempfile
        from tkinter import filedialog, messagebox
        p = filedialog.asksaveasfilename(
            defaultextension=".h", filetypes=[("header", "*.h")]
        )
        if not p:
            return
        tool = uitxt2h_path()
        if not os.path.isfile(tool):
            messagebox.showerror("导出 .h", "找不到 uitxt2h.py")
            return
        fd, tmp = tempfile.mkstemp(suffix=".uitxt")
        os.close(fd)
        try:
            self.write_uitxt(tmp)
            out = subprocess.check_output(
                [sys.executable, tool, tmp], stderr=subprocess.STDOUT
            )
            with open(p, "wb") as f:
                f.write(out)
        except subprocess.CalledProcessError as e:
            messagebox.showerror("导出 .h", e.output.decode("utf-8", "replace"))
        finally:
            try:
                os.remove(tmp)
            except OSError:
                pass

    def write_uitxt(self, path):
        lines = ['window "%s" %d %d' % (self.title, DESIGN_W, DESIGN_H)]
        for w in self.widgets:
            lines.append(
                '%s id=%s x=%d y=%d w=%d h=%d text="%s"' % (
                    w["kind"], w["id"], w["x"], w["y"], w["w"], w["h"],
                    w["text"].replace('"', '\\"')
                )
            )
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        self.status.config(text="已保存 %s（%d 个控件）" % (path, len(self.widgets)))

    def load_uitxt(self, path):
        self._push_undo()
        widgets = []
        title = "MyApp"
        with open(path, "r") as f:
            first = True
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                if first:
                    first = False
                    m = re.match(r'^window\s+"([^"]*)"\s+(\d+)\s+(\d+)\s*$', line)
                    if not m:
                        raise ValueError("bad window line")
                    title = m.group(1)
                    continue
                m = re.match(
                    r'^(label|button|checkbox|textbox)\s+(.+)$', line
                )
                if not m:
                    continue
                kind, rest = m.group(1), m.group(2)
                fields = {}
                for km in re.finditer(r'([a-z]+)=(?:"([^"]*)"|(\S+))', rest):
                    fields[km.group(1)] = (
                        km.group(2) if km.group(2) is not None else km.group(3)
                    )
                widgets.append({
                    "kind": kind,
                    "id": fields.get("id", self.next_id(kind)),
                    "text": fields.get("text", KIND_LABEL[kind]),
                    "x": int(fields.get("x", 0)),
                    "y": int(fields.get("y", 0)),
                    "w": int(fields.get("w", DEFAULT_W)),
                    "h": int(fields.get("h", DEFAULT_H)),
                })
        self.title = title
        self.widgets = widgets
        self.selected = None
        self.path = path
        self.sync_counters()
        self.refresh()

    def _set_props_enabled(self, on):
        st = self.tk.NORMAL if on else self.tk.DISABLED
        for e in self.ents.values():
            e.config(state=st)

    def fill_props(self):
        self._prop_lock = True
        if self.selected is None:
            for e in self.ents.values():
                e.config(state=self.tk.NORMAL)
                e.delete(0, self.tk.END)
            self._set_props_enabled(False)
        else:
            self._set_props_enabled(True)
            w = self.widgets[self.selected]
            for k in ("id", "text", "x", "y", "w", "h"):
                self.ents[k].delete(0, self.tk.END)
                self.ents[k].insert(0, str(w[k]))
        self._prop_lock = False

    def on_prop(self, _ev=None):
        if self._prop_lock or self.selected is None:
            return
        w = self.widgets[self.selected]
        try:
            nid = self.ents["id"].get().strip()
            if nid:
                w["id"] = nid
            w["text"] = self.ents["text"].get()
            w["x"] = max(0, int(self.ents["x"].get() or 0))
            w["y"] = max(0, int(self.ents["y"].get() or 0))
            w["w"] = max(8, int(self.ents["w"].get() or 8))
            w["h"] = max(8, int(self.ents["h"].get() or 8))
        except ValueError:
            return
        self.draw()
        self.update_status()

    def draw(self):
        c = self.canvas
        c.delete("all")
        # 设计区背景（1280×720 逻辑，居中）
        dx1, dy1 = self.cx(0), self.cy(0)
        dx2, dy2 = self.cx(DESIGN_W), self.cy(DESIGN_H)
        c.create_rectangle(dx1, dy1, dx2, dy2, fill="#ffffff", outline="#bbb", width=1)
        for x in range(0, DESIGN_W + 1, GRID):
            c.create_line(self.cx(x), dy1, self.cx(x), dy2, fill="#eee")
        for y in range(0, DESIGN_H + 1, GRID):
            c.create_line(dx1, self.cy(y), dx2, self.cy(y), fill="#eee")
        for i, w in enumerate(self.widgets):
            self.draw_widget(c, w, i == self.selected)

    def draw_widget(self, c, w, selected):
        x1, y1 = self.cx(w["x"]), self.cy(w["y"])
        x2, y2 = self.cx(w["x"] + w["w"]), self.cy(w["y"] + w["h"])
        k = w["kind"]
        if k == "button":
            # 凸起 3D：深蓝边 + 上/左白高光 + 下/右灰阴影
            c.create_rectangle(x1, y1, x2, y2, fill="#b8d0f0", outline="#2a5a9a", width=2)
            c.create_line(x1 + 2, y1 + 2, x2 - 2, y1 + 2, fill="#ffffff", width=2)
            c.create_line(x1 + 2, y1 + 2, x1 + 2, y2 - 2, fill="#ffffff", width=2)
            c.create_line(x2 - 2, y1 + 2, x2 - 2, y2 - 2, fill="#6080a8", width=2)
            c.create_line(x1 + 2, y2 - 2, x2 - 2, y2 - 2, fill="#6080a8", width=2)
            c.create_text((x1 + x2) / 2, (y1 + y2) / 2, text=w["text"][:24], fill="#111")
        elif k == "label":
            # 无边框纯文字
            c.create_text(x1 + 2, (y1 + y2) / 2, text=w["text"][:24], fill="#222", anchor="w")
        elif k == "checkbox":
            box = min(self.cx(20) - self.cx(0), x2 - x1, y2 - y1)
            c.create_rectangle(x1, y1, x1 + box, y1 + box, fill="#fff", outline="#333", width=2)
            # 勾（粗）
            c.create_line(x1 + 3, y1 + box * 0.55, x1 + box * 0.45, y1 + box - 4, fill="#111", width=3)
            c.create_line(x1 + box * 0.45, y1 + box - 4, x1 + box - 4, y1 + 4, fill="#111", width=3)
            c.create_text(x1 + box + 5, (y1 + y2) / 2, text=w["text"][:24], fill="#111", anchor="w")
        elif k == "textbox":
            # 内陷 3D：上/左灰暗线 + 下/右白高光，浅黄底
            c.create_rectangle(x1, y1, x2, y2, fill="#fff8d0", outline="#666", width=1)
            c.create_line(x1 + 1, y1 + 1, x2 - 1, y1 + 1, fill="#666", width=1)
            c.create_line(x1 + 1, y1 + 1, x1 + 1, y2 - 1, fill="#666", width=1)
            c.create_line(x2 - 1, y1 + 1, x2 - 1, y2 - 1, fill="#ffffff", width=1)
            c.create_line(x1 + 1, y2 - 1, x2 - 1, y2 - 1, fill="#ffffff", width=1)
            c.create_text(x1 + 4, (y1 + y2) / 2, text=w["text"][:24], fill="#333", anchor="w")
        # 选中：虚线框 + 右下角 handle
        if selected:
            c.create_rectangle(x1 - 2, y1 - 2, x2 + 2, y2 + 2, outline="#2060c0", width=2, dash=(4, 2))
            c.create_rectangle(
                x2 - HANDLE * self.sx, y2 - HANDLE * self.sy, x2, y2,
                fill="#2060c0", outline="#2060c0"
            )

    def update_status(self):
        msg = "%d 个控件" % len(self.widgets)
        if self.selected is not None:
            msg += " | 选中 %s" % self.widgets[self.selected]["id"]
        if self.place_kind:
            msg += " | 待放置 %s" % KIND_LABEL[self.place_kind]
        msg += " | 撤销%d 重做%d" % (len(self.history), len(self.future))
        self.status.config(text=msg)

    def refresh(self):
        self.draw()
        self.fill_props()
        self.update_status()

    def run(self):
        self.root.mainloop()


def main(argv):
    if "--help" in argv or "-h" in argv:
        usage()
        return 0
    try:
        import tkinter as tk
        from tkinter import ttk  # noqa: F401 — 可用性探测
    except ImportError:
        sys.stderr.write(
            "uitxt designer: need Tkinter.\n"
            "  Debian/Ubuntu: sudo apt install python3-tk\n"
        )
        usage()
        return 1
    DesignerApp(tk, None).run()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
