# ToyOS SDK

静态应用 SDK：头文件 + `libtoyos.a` / libToy* + `user.ld` + `ToySdk.mk` + 示例。  
宿主只需 **gcc / ld / make**。不必把内核树当 include 路径。

本文件随 `./Tools/build-sdk.sh` 拷到 SDK **根目录**；链接按解压后的树写。

`VERSION` 是 SDK **包**版本（与 CRT `TOYOS_CRT` 无关）。

## 5 分钟：解压即可 make

拿到 `ToySdk.tar.gz`（或已解压的 `ToySdk/`）后，放到**任意目录**：

```bash
tar xzf ToySdk.tar.gz          # 得到 ./ToySdk/
make -C ToySdk/Examples/Hello  # → Examples/Hello/Build/MYAPP.ELF
```

入口应在 `0x40000000`。其余示例：`File` `Dir` `Pipe` `Fork` `Gui` `Blit` `Net` `Fs`（见 [`Examples/README.md`](Examples/README.md)）。

进 Guest：把 ELF 拷到 ToyImage 的 `rootfs/`（FAT 8.3，文件名大写），或：

```bash
make -C ToySdk/Examples/Hello deploy TOYIMAGE=/path/to/ToyImage
# ToyImage 侧：./run-split.sh
# Guest 串口：exec MYAPP.ELF
```

`make deploy` 在 SDK 不在 `ToyKernel/Dist/ToySdk` 时**必须**设 `TOYIMAGE=`（默认兄弟仓推算会错）。

不要在仓库的 `Tools/Sdk/Examples/` 下直接 `make`（那里没有 `Library/`）。

## 目录

```
ToySdk/
├── include/               应用可见头（Unix CRT 名保持小写）
├── Library/               libtoyos.a、libToyUi.a、libToyGfx.a、libToyNet.a、libFsUtil.a、crt0.o、syscall.o
├── user.ld                x86 用户 ELF @ 0x40000000
├── Documents/             应用开发指南.md、API速查.md
├── Examples/              Hello File Dir Pipe Fork Gui Blit Net Fs
├── ToySdk.mk              应用 include 本文件
├── VERSION                SDK 包版本（如 1.0.0）
└── README.md              本文件
```

文档：[`Documents/应用开发指南.md`](Documents/应用开发指南.md)、[`Documents/API速查.md`](Documents/API速查.md)。

## 自己的应用

```makefile
TOYSDK ?= /path/to/ToySdk
PROG   ?= MYAPP
SRCS   ?= main.c
# GUI：EXTRA_LIBS = $(TOYSDK)/Library/libToyUi.a $(TOYSDK)/Library/libToyGfx.a
# 网络：EXTRA_LIBS = $(TOYSDK)/Library/libToyNet.a
# 路径：EXTRA_LIBS = $(TOYSDK)/Library/libFsUtil.a
include $(TOYSDK)/ToySdk.mk
```

`TOYSDK` 可省略：Makefile 与 `ToySdk.mk` 同树时，规则文件会 `abspath` 到 SDK 根。

## 仓库内重新打包（维护者）

```bash
cd ToyKernel && ./Tools/build-sdk.sh
# → Dist/ToySdk/ 与 Dist/ToySdk.tar.gz（gitignore，不入库）
```

课堂树内模板仍是 `User/Pkg/`（不依赖本 SDK）。
