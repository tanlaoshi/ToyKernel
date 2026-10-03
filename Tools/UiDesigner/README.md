# ToyOS UI Designer（PC）

PC 端拖控件 → 导出 `.uitxt` → `uitxt2h` → `MyAppUi.h` → 编进 Guest ELF。  
设计器**不**跑在 ToyOS 上；Guest 只做 `ToyUiLoadWindow` + 业务逻辑。

## 运行

```text
python3 Tools/UiDesigner/designer.py
python3 Tools/UiDesigner/designer.py --help
```

依赖：**Python 3 + Tkinter**（标准库组件；Debian/Ubuntu：`sudo apt install python3-tk`）。  
无第三方库（无 PIL/numpy）。

## 工作流

1. 打开设计器，从左侧点 Button/Label/…，再在画布点击放置  
2. 拖动移动；右下角小块改大小；属性栏改 id/text/几何  
3. **导出 → 导出 .uitxt**（或文件 → 保存）  
4. 拷到 `CodeE-User/Pkg/UiDesigner/MyApp.uitxt`（文档或写 `User/Pkg/`）  
5. `make -C CodeE-User/Pkg/UiDesigner`  
6. 填 `UiActions.c` 的 `OnId*`  
7. 拷 ELF 到 RootFs 后 `exec`

导出 `.h` 会调用 `Tools/UiLayout/uitxt2h.py`（须已落地）。

## ToyOS 侧

- 头：`Include/ToyUiLayout.h`  
- 实现：`Common/Services/ToyUiLayout/ToyUiLayout.c`（**只链进 Guest**，不进内核镜像）  
- `ToyUiScreenWidth()` 现占位 **1280**；scale = ScreenW / DesignW  
- 字体档位：约定已写，尚无公开换字号 API（不改 `ToyUi.h`）

## 限制

见 [`Documents/开发/拖控件设计器.md`](../../Documents/开发/拖控件设计器.md) §9。
