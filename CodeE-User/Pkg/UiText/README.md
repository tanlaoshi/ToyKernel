# UiText — 布局文件模板

`.uitxt` → `uitxt2h` → `MyAppUi.h` → `build/MYAPP.ELF`。运行时**不**解析布局。

规范：[`Documents/开发/布局文件格式.md`](../../../Documents/开发/布局文件格式.md)。

## 用法

1. 改 `MyApp.uitxt`（加 `label` / `button` / `checkbox` / `textbox`）  
2. `make`（在本目录）生成 `MyAppUi.h` 并编出 `build/MYAPP.ELF`  
3. 在 `UiActions.c` 填 `OnId*` 的 `/* TODO */`  
4. 拷到 `ToyImage/RootFs/X64/` 后 Guest：`exec MYAPP.ELF`  

```text
make -C CodeE-User/Pkg/UiText
python3 ../../../Tools/UiLayout/uitxt2h.py MyApp.uitxt | head
```

## 注意

- 不改 `ToyUi.h` ABI；`ToyUiLoadWindow` 只在本模板  
- 按钮物理 id 现为 0..3；checkbox/textbox 上限见 `ToyUi.h`  
- 树路径：`CodeE-User/Pkg/`（旧文档或写 `User/Pkg/`）
