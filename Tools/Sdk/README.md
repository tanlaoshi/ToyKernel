# ToyOS SDK（PR-A-sdk-pack）

给**应用开发者**的静态 SDK：头文件 + `libtoyos.a` / libToy* + 链接脚本 + `ToySdk.mk`。  
不需要把内核编进应用；宿主只需 gcc / ld / make。

由仓库根生成：

```bash
cd ToyKernel && ./Tools/build-sdk.sh
# 输出 Dist/ToySdk/（不入库）
```

## 目录

```
ToySdk/
├── include/               应用可见头（与 User/include 同步；Unix CRT 名保持小写）
├── Library/               libtoyos.a、libToyUi.a、libToyGfx.a、libToyNet.a、crt0.o、syscall.o
├── user.ld                x86 用户 ELF @ 0x40000000
├── Documents/             应用开发指南.md
├── Examples/Hello/        最小 CRT 示例
└── ToySdk.mk              应用 include 本文件
```

## 编译示例

```bash
cd Dist/ToySdk/Examples/Hello
make                  # → Build/MYAPP.ELF
make deploy           # 复制到兄弟仓 ToyImage/rootfs/（可设 TOYIMAGE=）
```

自己的应用：

```makefile
TOYSDK ?= /path/to/ToySdk
PROG   ?= MYAPP
SRCS   ?= main.c
# GUI：EXTRA_LIBS = $(TOYSDK)/Library/libToyUi.a $(TOYSDK)/Library/libToyGfx.a
# 网络：EXTRA_LIBS = $(TOYSDK)/Library/libToyNet.a
include $(TOYSDK)/ToySdk.mk
```

课堂仍可用仓库内 `User/Pkg/`（不依赖本 SDK）。说明见 [`应用开发指南.md`](../../Documents/应用开发指南.md)。
