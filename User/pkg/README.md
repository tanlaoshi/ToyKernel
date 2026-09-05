# 用户程序模板（PR-L1）

用 ToyOS CRT（`<stdio.h>` / `libtoyos`）编一个可 `exec` 的 ELF。

## 快速开始

```bash
# 先编好内核侧 CRT / libtoyos（或直接 make，模板会代调）
cd /path/to/ToyKernel && ./build.sh

cd User/pkg
make                 # → build/MYAPP.ELF
# 拷到 rootfs（FAT 8.3 大写）
cp build/MYAPP.ELF ../../ToyImage/rootfs/MYAPP.ELF
# QEMU 里：
#   toyos> exec MYAPP.ELF
```

换名：

```bash
make PROG=HELLO2 SRCS=main.c
```

课外独立目录：复制 `User/pkg/`，保留或改 `main.c`，构建时指定树根：

```bash
make TOYKERNEL=/path/to/ToyKernel
```

## 头文件

| Include | 用途 |
|---------|------|
| `<stdio.h>` / `<stdlib.h>` / `<string.h>` / `<unistd.h>` | libc 子集 |
| `<fcntl.h>` / `<errno.h>` / `<signal.h>` | 文件与信号 |
| `<toyos/syscall.h>` | 系统调用号 + `toy_*` 薄封装 |
| `<toyos/version.h>` | `TOYOS_CRT_VERSION_*` |
| `<toyos.h>` | 伞头（demo 用） |

`-I` 指向 `ToyKernel/User/include`（由 `ToyUser.mk` 自动加）。

## 链接

`crt0.o` + `syscall.o` + `libtoyos.a`（string/printf/malloc/errno/unistd）+ `user.ld`。

GUI 程序可追加：

```make
EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a
```

（先在 ToyKernel 里 `make` 生成这两个 `.a`。）

## 不要做的事

- 不要 `#include` 内核 `Include/` 或调用 `Hal*`
- 不要依赖宿主 glibc（`-nostdlib`）
