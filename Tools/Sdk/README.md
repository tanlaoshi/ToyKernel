# ToyOS SDK

静态应用 SDK：头文件 + `libtoyos.a` / libToy* + `user.ld` + `ToySdk.mk` + 示例。  
宿主只需 **gcc / ld / make**。不必把内核树当 include 路径。

本文件随 `./Tools/build-sdk.sh` 拷到 SDK **根目录**；链接按解压后的树写。

`VERSION` 是 SDK **包**版本（与 CRT `TOYOS_CRT` 无关）。开课冻结包：**`1.0.0-course`**。

打包（产物在 `Build/ToySDK/`，随 `Build/` gitignore，勿提交二进制）：

```bash
./Tools/build-sdk.sh          # → Build/ToySDK/ + Build/ToySDK.tar.gz
tar xzf Build/ToySDK.tar.gz -C /tmp && make -C /tmp/ToySDK/Examples/Hello
```

## 5 分钟：解压即可 make

拿到 `ToySDK.tar.gz`（或已解压的 `ToySDK/`）后，放到**任意目录**：

```bash
tar xzf ToySDK.tar.gz          # 得到 ./ToySDK/
make -C ToySDK/Examples/Hello  # → Examples/Hello/Build/MYAPP.ELF
```

入口应在 `0x40000000`。其余示例：`File` `Dir` `Pipe` `Fork` `Gui` `Blit` `Net` `Fs`（见 [`Examples/README.md`](Examples/README.md)）。

进 Guest：把 ELF 拷到 ToyImage 的 `RootFs/X64/`（FAT 8.3，文件名大写），或：

```bash
make -C ToySDK/Examples/Hello deploy TOYIMAGE=/path/to/ToyImage
# ToyImage 侧：./run-split.sh
# Guest 串口：exec MYAPP.ELF
```

`make deploy` 在 SDK 不在 `ToyKernel/Build/ToySDK` 时**必须**设 `TOYIMAGE=`（默认兄弟仓推算会错）。

不要在仓库的 `Tools/Sdk/Examples/` 下直接 `make`（那里没有 `Library/`）。

## 目录

```
ToySDK/
├── include/               应用可见头（含 SyscallABI.h；Unix CRT 名保持小写）
├── Library/               libtoyos.a、libToyUi.a、libToyGfx.a、libToyNet.a、libFsUtil.a、crt0.o、syscall.o
├── user.ld                x86 用户 ELF @ 0x40000000
├── Documents/             应用开发指南.md、API速查.md
├── Examples/              Hello File Dir Pipe Fork Gui Blit Net Fs
├── ToySdk.mk              应用 include 本文件
├── Makefile.template      复制到应用目录；只 include ToySdk.mk
├── VERSION                SDK 包版本（如 1.0.0-course）
└── README.md              本文件
```

文档：[`Documents/应用开发指南.md`](Documents/应用开发指南.md)、[`Documents/API速查.md`](Documents/API速查.md)。

## 自己的应用

复制 [`Makefile.template`](Makefile.template) 到应用目录（与 `main.c` 同级），设 `TOYSDK` 为 SDK 根：

```bash
cp ToySDK/Makefile.template myapp/Makefile
# 编辑 Makefile：TOYSDK ?= /path/to/ToySDK
make -C myapp
```

模板只 `include $(TOYSDK)/ToySdk.mk`，不要再抄 CFLAGS。需要 GUI / 网 / 路径库时解开模板里的 `EXTRA_LIBS` 注释。

`TOYSDK` 在 Makefile 与 `ToySdk.mk` **同目录**时可省略（规则文件会 `abspath` 到 SDK 根）。复制走之后必须设。

## 仓库内重新打包（维护者）

```bash
cd ToyKernel && ./Tools/build-sdk.sh
# → Build/ToySDK/ 与 Build/ToySDK.tar.gz（gitignore，不入库）
```

课堂树内模板仍是 `User/Pkg/`（不依赖本 SDK）。
