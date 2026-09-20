ARCH ?= x86_64
# PR-B2：Arm/RiscV 板包选择 → HAL/<Arch>/Board/<board>/；x86 桌面真机走 1.3c，忽略 BOARD
BOARD ?= virt
DEBUG ?= 0
NO_COM1 ?= 0
# 串口/屏幕总开关 + 分模块（见 Include/ToySerialConfig.h）；NO_COM1=1 ⇒ SERIAL=0
SERIAL ?= 1
SERIAL_BOOT ?= 1
SERIAL_USB ?= 1
SERIAL_SMP ?= 1
SERIAL_GUI ?= 1
SERIAL_NET ?= 1
SERIAL_FS ?= 1
SERIAL_MEM ?= 1
SERIAL_DRV ?= 1
SERIAL_MISC ?= 1
SCREEN_LOG ?= 1
SCREEN_LOG_BOOT ?= 1
SCREEN_LOG_USB ?= 1
SCREEN_LOG_SMP ?= 0
SCREEN_LOG_GUI ?= 1
SCREEN_LOG_NET ?= 0
SCREEN_LOG_FS ?= 1
SCREEN_LOG_MEM ?= 0
SCREEN_LOG_DRV ?= 0
SCREEN_LOG_MISC ?= 0
ifeq ($(NO_COM1),1)
SERIAL := 0
endif
ifeq ($(SERIAL),0)
SERIAL_BOOT := 0
SERIAL_USB := 0
SERIAL_SMP := 0
SERIAL_GUI := 0
SERIAL_NET := 0
SERIAL_FS := 0
SERIAL_MEM := 0
SERIAL_DRV := 0
SERIAL_MISC := 0
endif
ifeq ($(SCREEN_LOG),0)
SCREEN_LOG_BOOT := 0
SCREEN_LOG_USB := 0
SCREEN_LOG_SMP := 0
SCREEN_LOG_GUI := 0
SCREEN_LOG_NET := 0
SCREEN_LOG_FS := 0
SCREEN_LOG_MEM := 0
SCREEN_LOG_DRV := 0
SCREEN_LOG_MISC := 0
endif
LWIP ?= 1
LWIPINCLUDES :=
LWIPOBJS :=
LWIP_PORT_OBJS :=
BOARD_OBJS :=

# PR-A6/A7：非 x86 默认可链接完整 Common（BRINGUP=1 仍为串口 hello）
ifeq ($(ARCH),x86_64)
BRINGUP ?= 0
else
BRINGUP ?= 0
endif

CC = gcc
LD = ld
OBJCOPY = objcopy

# 可选：Tools/Extract 下的 xPack / 交叉工具链（见 Tools/README.md）
TOOLS_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/Tools/Extract)

ifeq ($(ARCH),x86_64)
HAL_ARCH = X64
# -mgeneral-regs-only：禁止内核生成 SSE/AVX（未开 CR4.OSFXSR 时 pxor %xmm 会 #UD）
ARCH_CFLAGS = -m64 -mno-red-zone -mgeneral-regs-only
USER_BLOB_FMT = elf64-x86-64
LDFLAGS_ARCH =
endif
ifeq ($(ARCH),riscv)
HAL_ARCH = RiscV
ARCH_CFLAGS = -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
LDFLAGS_ARCH =
ifneq ($(wildcard $(TOOLS_ROOT)/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc),)
TOOLS_RISCV := $(lastword $(sort $(wildcard $(TOOLS_ROOT)/xpack-riscv-none-elf-gcc-*)))
CC = $(TOOLS_RISCV)/bin/riscv-none-elf-gcc
LD = $(TOOLS_RISCV)/bin/riscv-none-elf-ld
OBJCOPY = $(TOOLS_RISCV)/bin/riscv-none-elf-objcopy
else ifneq ($(shell command -v riscv64-linux-gnu-gcc 2>/dev/null),)
CC = riscv64-linux-gnu-gcc
LD = riscv64-linux-gnu-ld
OBJCOPY = riscv64-linux-gnu-objcopy
else ifneq ($(shell command -v riscv64-unknown-elf-gcc 2>/dev/null),)
CC = riscv64-unknown-elf-gcc
LD = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
endif
endif
ifeq ($(ARCH),arm64)
HAL_ARCH = Arm64
ARCH_CFLAGS = -mgeneral-regs-only
LDFLAGS_ARCH =
ifneq ($(wildcard $(TOOLS_ROOT)/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc),)
TOOLS_ARM := $(lastword $(sort $(wildcard $(TOOLS_ROOT)/xpack-aarch64-none-elf-gcc-*)))
CC = $(TOOLS_ARM)/bin/aarch64-none-elf-gcc
LD = $(TOOLS_ARM)/bin/aarch64-none-elf-ld
OBJCOPY = $(TOOLS_ARM)/bin/aarch64-none-elf-objcopy
else ifneq ($(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),)
CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
else ifneq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
CC = aarch64-none-elf-gcc
LD = aarch64-none-elf-ld
OBJCOPY = aarch64-none-elf-objcopy
endif
endif

INCLUDES_COMMON = -IInclude \
                  -ICommon/Library \
                  -IFonts \
                  -IHAL/$(HAL_ARCH) \
                  -IHAL/$(HAL_ARCH)/Hal
INCLUDES_HAL    = $(INCLUDES_COMMON) \
                  -IHAL/$(HAL_ARCH)/Drivers

# PR-B2：非 x86 必须选中 Board 包（默认 virt）；Common 不 -I 板目录
ifeq ($(ARCH),x86_64)
BOARD_DIR :=
else
BOARD_DIR := HAL/$(HAL_ARCH)/Board/$(BOARD)
ifeq ($(wildcard $(BOARD_DIR)/README.md),)
$(error BOARD=$(BOARD): missing $(BOARD_DIR)/ (see HAL/Board/README.md); default BOARD=virt)
endif
INCLUDES_HAL += -I$(BOARD_DIR)
endif

XHCI_DIAG_VERBOSE ?= 0
TOY_DEMO_DRIVER ?= 1
CFLAGS_BASE = -ffreestanding -nostdlib -O2 -Wall -Wextra \
              -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
              -DTOY_KERNEL_DEBUG=$(DEBUG) -DTOY_BRINGUP=$(BRINGUP) \
              -DTOY_SERIAL=$(SERIAL) \
              -DTOY_SERIAL_BOOT=$(SERIAL_BOOT) \
              -DTOY_SERIAL_USB=$(SERIAL_USB) \
              -DTOY_SERIAL_SMP=$(SERIAL_SMP) \
              -DTOY_SERIAL_GUI=$(SERIAL_GUI) \
              -DTOY_SERIAL_NET=$(SERIAL_NET) \
              -DTOY_SERIAL_FS=$(SERIAL_FS) \
              -DTOY_SERIAL_MEM=$(SERIAL_MEM) \
              -DTOY_SERIAL_DRV=$(SERIAL_DRV) \
              -DTOY_SERIAL_MISC=$(SERIAL_MISC) \
              -DTOY_SCREEN_LOG=$(SCREEN_LOG) \
              -DTOY_SCREEN_LOG_BOOT=$(SCREEN_LOG_BOOT) \
              -DTOY_SCREEN_LOG_USB=$(SCREEN_LOG_USB) \
              -DTOY_SCREEN_LOG_SMP=$(SCREEN_LOG_SMP) \
              -DTOY_SCREEN_LOG_GUI=$(SCREEN_LOG_GUI) \
              -DTOY_SCREEN_LOG_NET=$(SCREEN_LOG_NET) \
              -DTOY_SCREEN_LOG_FS=$(SCREEN_LOG_FS) \
              -DTOY_SCREEN_LOG_MEM=$(SCREEN_LOG_MEM) \
              -DTOY_SCREEN_LOG_DRV=$(SCREEN_LOG_DRV) \
              -DTOY_SCREEN_LOG_MISC=$(SCREEN_LOG_MISC) \
              -DXHCI_DIAG_VERBOSE=$(XHCI_DIAG_VERBOSE) \
              -DTOY_DEMO_DRIVER=$(TOY_DEMO_DRIVER) \
              -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
              $(ARCH_CFLAGS)

ifeq ($(LWIP),1)
CFLAGS_BASE += -DTOY_LWIP=1
LWIPDIR = ThirdParty/lwip/src
LWIPINCLUDES = -I$(LWIPDIR)/include \
               -IHAL/$(HAL_ARCH)/LwIp/include \
               -IHAL/$(HAL_ARCH)/LwIp
LWIPCORE = \
	$(LWIPDIR)/core/init.c \
	$(LWIPDIR)/core/def.c \
	$(LWIPDIR)/core/inet_chksum.c \
	$(LWIPDIR)/core/ip.c \
	$(LWIPDIR)/core/mem.c \
	$(LWIPDIR)/core/memp.c \
	$(LWIPDIR)/core/netif.c \
	$(LWIPDIR)/core/pbuf.c \
	$(LWIPDIR)/core/raw.c \
	$(LWIPDIR)/core/stats.c \
	$(LWIPDIR)/core/sys.c \
	$(LWIPDIR)/core/tcp.c \
	$(LWIPDIR)/core/tcp_in.c \
	$(LWIPDIR)/core/tcp_out.c \
	$(LWIPDIR)/core/timeouts.c \
	$(LWIPDIR)/core/udp.c \
	$(LWIPDIR)/core/dns.c \
	$(LWIPDIR)/core/ipv4/etharp.c \
	$(LWIPDIR)/core/ipv4/icmp.c \
	$(LWIPDIR)/core/ipv4/ip4.c \
	$(LWIPDIR)/core/ipv4/ip4_addr.c \
	$(LWIPDIR)/core/ipv4/dhcp.c \
	$(LWIPDIR)/netif/ethernet.c
LWIPOBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(LWIPCORE))
LWIP_PORT_SRCS = HAL/$(HAL_ARCH)/LwIp/toy_netif.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_ping.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_tcpecho.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_udp.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_tcpclient.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_socket.c
LWIP_PORT_OBJS = $(patsubst HAL/$(HAL_ARCH)/LwIp/%.c,$(HALDIR)/LwIp/%.o,$(LWIP_PORT_SRCS))
endif

CFLAGS_COMMON = $(CFLAGS_BASE) $(INCLUDES_COMMON) $(LWIPINCLUDES)
CFLAGS_HAL    = $(CFLAGS_BASE) $(INCLUDES_HAL) $(LWIPINCLUDES) -IHAL/$(HAL_ARCH)/LwIp
ifneq ($(BOARD_DIR),)
CFLAGS_HAL += -DTOY_BOARD=\"$(BOARD)\"
endif

LDFLAGS = -nostdlib -static -z noexecstack -T HAL/$(HAL_ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)
# SpinLock 的 __sync_* 需要 libgcc（如 __aarch64_swp4_sync）
LIBGCC := $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

# Build/ 镜像源码树：Common、Fonts 与 HAL 同级；HAL 下按 Arch 分目录。
# Common/Fonts 的 .o 随当前 ARCH 编译（不可三架构并存同一套 .o）；clean 会清掉它们。
# 各 Arch 的 Kernel.elf / HAL .o 留在 Build/HAL/<Arch>/，换架构 clean 不删其它 Arch 成品。
BUILDDIR = Build
HALDIR = $(BUILDDIR)/HAL/$(HAL_ARCH)

# Common/Fonts/.o 跨 Arch 共用路径：换 ARCH 时若仍用旧 .o 会链错格式（如 EM:183 aarch64 → riscv）
ARCH_STAMP := $(BUILDDIR)/.toy_arch
ifneq ($(wildcard $(BUILDDIR)/Common),)
_STAMP_ARCH := $(shell cat $(ARCH_STAMP) 2>/dev/null)
ifneq ($(_STAMP_ARCH),$(ARCH))
$(info ARCH: stale Common '$(_STAMP_ARCH)' → '$(ARCH)'; cleaning Build/Common Fonts lwip)
$(shell rm -rf '$(BUILDDIR)/Common' '$(BUILDDIR)/Fonts' '$(BUILDDIR)/ThirdParty/lwip' '$(BUILDDIR)/lwip')
endif
endif
$(shell mkdir -p '$(BUILDDIR)' && echo '$(ARCH)' > '$(ARCH_STAMP)')

# PR-B3：同 HALDIR 多板包共用；BOARD 与 .toy_board 不一致（或缺 stamp）时清 HAL，避免链错 UART/Config
ifneq ($(ARCH),x86_64)
BOARD_STAMP := $(HALDIR)/.toy_board
ifneq ($(wildcard $(HALDIR)/Kernel.elf),)
_STAMP_BOARD := $(shell cat $(BOARD_STAMP) 2>/dev/null)
ifneq ($(_STAMP_BOARD),$(BOARD))
$(info BOARD: stale build '$(_STAMP_BOARD)' → '$(BOARD)'; cleaning $(HALDIR))
$(shell rm -rf '$(HALDIR)')
endif
endif
endif

# PR-D-tpl-2：TOY_DEMO_DRIVER 变了必须重编 HalDevices（裸 make 换宏否则会「无需做任何事」）
DEMO_STAMP := $(HALDIR)/.toy_demo_driver
.PHONY: FORCE
FORCE:
$(DEMO_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(TOY_DEMO_DRIVER)' > $@.new
	@if [ ! -f $@ ] || ! cmp -s $@.new $@; then mv $@.new $@; else rm -f $@.new; fi

CORE_SRCS     := $(wildcard Common/Core/*.c)
CORE_SRCS     += $(wildcard Common/Core/Scheduler/*.c)
CORE_SRCS     += $(wildcard Common/Core/Process/*.c)
CORE_SRCS     += $(wildcard Common/Core/TaskFd/*.c)
CORE_SRCS     += $(wildcard Common/Core/Syscall/*.c)
CORE_SRCS     += $(wildcard Common/Core/VirtualMemory/*.c)
SERVICES_SRCS := $(wildcard Common/Services/*.c)
# Services/*.c 不进子目录；每个模块开目录时补一行
SERVICES_SRCS += $(wildcard Common/Services/GuiDrag/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiDraw/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiPointer/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiUser/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiOpen/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiBackup/*.c)
SERVICES_SRCS += $(wildcard Common/Services/GuiFocus/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Console/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Desktop/*.c)
SERVICES_SRCS += $(wildcard Common/Services/FilesUi/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Store/*.c)
SERVICES_SRCS += $(wildcard Common/Services/SettingsUi/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Theme/*.c)
SERVICES_SRCS += $(wildcard Common/Services/StoreUi/*.c)
SERVICES_SRCS += $(wildcard Common/Services/EditUi/*.c)
SERVICES_SRCS += $(wildcard Common/Services/StoreNet/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Db/*.c)
SERVICES_SRCS += $(wildcard Common/Services/FileSystem/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Tasks/*.c)
SERVICES_SRCS += $(wildcard Common/Services/ShellCommands/*.c)
SERVICES_SRCS += $(wildcard Common/Services/Tcp/*.c)
LIB_SRCS      := $(wildcard Common/Library/*.c)
LIB_SRCS      += $(wildcard Common/Library/Fat/*.c)
LIB_SRCS      += $(wildcard Common/Library/Gpt/*.c)
FONT_SRCS     := $(wildcard Common/Fonts/*.c)
DRIVER_SRCS   := $(wildcard HAL/$(HAL_ARCH)/Drivers/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Video/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Net/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/E1000/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Nvme/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Ahci/*.c)
# PR-H-xhci-split-8：Drivers/XHCI/*.c（Core/Port/Device/Hid/Hub/Mouse/Irq/Diag）；已删单体 Drivers/XHCI.c
XHCI_SPLIT_SRCS := $(wildcard HAL/$(HAL_ARCH)/Drivers/XHCI/*.c)
DRIVER_SRCS   += $(XHCI_SPLIT_SRCS)
ARCH_SRCS     := $(wildcard HAL/$(HAL_ARCH)/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Hal/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/AcpiMadt/*.c)
ARCH_ASM_ALL  := $(wildcard HAL/$(HAL_ARCH)/*.S)
ARCH_ASM      := $(filter-out HAL/$(HAL_ARCH)/SmpTrampoline.S HAL/$(HAL_ARCH)/Startup.S,$(ARCH_ASM_ALL))

CORE_OBJS     := $(patsubst Common/Core/%.c,$(BUILDDIR)/Common/Core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst Common/Services/%.c,$(BUILDDIR)/Common/Services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst Common/Library/%.c,$(BUILDDIR)/Common/Library/%.o,$(LIB_SRCS))
FONT_OBJS     := $(patsubst Common/Fonts/%.c,$(BUILDDIR)/Common/Fonts/%.o,$(FONT_SRCS))
DRIVER_OBJS   := $(patsubst HAL/$(HAL_ARCH)/Drivers/%.c,$(HALDIR)/Drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst HAL/$(HAL_ARCH)/%.c,$(HALDIR)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst HAL/$(HAL_ARCH)/%.S,$(HALDIR)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(HALDIR)/User_hello_blob.o $(HALDIR)/SmpTramp_blob.o
# 演示 ELF/OBJ 进 Build/User/（源在 User/Apps/）；CRT/Library 仍就地编
USER_OUT = $(BUILDDIR)/User
USER_HELLO_ELF = $(USER_OUT)/hello.elf
USER_COUNT_ELF = $(USER_OUT)/count.elf
USER_FORK_ELF = $(USER_OUT)/fork.elf
USER_WAITNH_ELF = $(USER_OUT)/waitnh.elf
USER_DYNDEMO_ELF = $(USER_OUT)/dyndemo.elf
USER_LIBTOY_SO = $(USER_OUT)/libtoy.so
USER_CAT_ELF = $(USER_OUT)/catfile.elf
USER_WRITE_ELF = $(USER_OUT)/writefile.elf
USER_NETDEMO_ELF = $(USER_OUT)/netdemo.elf
USER_NETSRV_ELF = $(USER_OUT)/netsrv.elf
USER_SYSHELLO_ELF = $(USER_OUT)/syshello.elf
USER_SYSFORK_ELF = $(USER_OUT)/sysfork.elf
USER_EXECDEMO_ELF = $(USER_OUT)/execdemo.elf
USER_PIPEDEMO_ELF = $(USER_OUT)/pipedemo.elf
USER_BRKDEMO_ELF = $(USER_OUT)/brkdemo.elf
USER_MMAPDEMO_ELF = $(USER_OUT)/mmapdemo.elf
USER_KILLDEMO_ELF = $(USER_OUT)/killdemo.elf
USER_SIGDEMO_ELF = $(USER_OUT)/sigdemo.elf
USER_WINDEMO_ELF = $(USER_OUT)/windemo.elf
USER_GUIDEMO_ELF = $(USER_OUT)/guidemo.elf
USER_BLITDEMO_ELF = $(USER_OUT)/blitdemo.elf
USER_LIBCDEMO_ELF = $(USER_OUT)/libcdemo.elf
USER_DIRDEMO_ELF = $(USER_OUT)/dirdemo.elf
USER_NETLIB_ELF = $(USER_OUT)/netlibdemo.elf
USER_HELLO_OBJ = $(USER_OUT)/hello.o
USER_COUNT_OBJ = $(USER_OUT)/count.o
USER_FORK_OBJ = $(USER_OUT)/fork.o
USER_WAITNH_OBJ = $(USER_OUT)/waitnh.o
USER_DYNDEMO_OBJ = $(USER_OUT)/dyndemo.o
USER_LIBTOY_OBJ = $(USER_OUT)/libtoy.o
USER_CAT_OBJ = $(USER_OUT)/cat.o
USER_WRITE_OBJ = $(USER_OUT)/writefile.o
USER_NETDEMO_OBJ = $(USER_OUT)/netdemo.o
USER_NETSRV_OBJ = $(USER_OUT)/netsrv.o
USER_SYSHELLO_OBJ = $(USER_OUT)/syshello.o
USER_SYSFORK_OBJ = $(USER_OUT)/sysfork.o
USER_EXECDEMO_OBJ = $(USER_OUT)/execdemo.o
USER_PIPEDEMO_OBJ = $(USER_OUT)/pipedemo.o
USER_BRKDEMO_OBJ = $(USER_OUT)/brkdemo.o
USER_MMAPDEMO_OBJ = $(USER_OUT)/mmapdemo.o
USER_KILLDEMO_OBJ = $(USER_OUT)/killdemo.o
USER_SIGDEMO_OBJ = $(USER_OUT)/sigdemo.o
USER_WINDEMO_OBJ = $(USER_OUT)/windemo.o
USER_GUIDEMO_OBJ = $(USER_OUT)/guidemo.o
USER_BLITDEMO_OBJ = $(USER_OUT)/blitdemo.o
USER_LIBCDEMO_OBJ = $(USER_OUT)/libcdemo.o
USER_DIRDEMO_OBJ = $(USER_OUT)/dirdemo.o
USER_NETLIB_OBJ = $(USER_OUT)/netlibdemo.o
USER_LIB_TOY_GFX_OBJ = User/Library/ToyGfx/ToyGfx.o
USER_LIB_TOY_UI_OBJ = User/Library/ToyUi/ToyUi.o
USER_LIB_TOY_UI_WIDGETS_OBJ = User/Library/ToyUi/ToyUiWidgets.o
USER_LIB_TOY_NET_OBJ = User/Library/ToyNet/ToyNet.o
USER_LIB_FSUTIL_OBJ = User/Library/FsUtil/FsUtil.o
USER_LIB_TOY_GFX_A = User/Library/ToyGfx/libToyGfx.a
USER_LIB_TOY_UI_A = User/Library/ToyUi/libToyUi.a
USER_LIB_TOY_NET_A = User/Library/ToyNet/libToyNet.a
USER_LIB_FSUTIL_A = User/Library/FsUtil/libFsUtil.a
USER_LIB_TOYOS_A = User/Library/ToyOs/libtoyos.a
USER_LIB_TOYOS_OBJS = User/crt/string.o User/crt/printf.o User/crt/malloc.o \
	User/crt/errno.o User/crt/unistd.o User/crt/stdlib.o User/crt/signal.o \
	User/crt/dirent.o User/crt/stdio.o
USER_LD = User/user.ld
USER_LDFLAGS = -z noexecstack
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone -IUser/include
USER_CRT_OBJS = User/crt/crt0.o User/crt/syscall.o $(USER_LIB_TOYOS_OBJS)
else
EXTRA_OBJS = $(HALDIR)/Startup_asm.o
# PR-B2：Board 包 .c（BoardConfig.h 仅 HAL -I；不进 Common）
BOARD_SRCS := $(wildcard $(BOARD_DIR)/*.c)
BOARD_OBJS := $(patsubst $(BOARD_DIR)/%.c,$(HALDIR)/Board/%.o,$(BOARD_SRCS))
EXTRA_OBJS += $(BOARD_OBJS)
# PR-R5：Arm/RiscV 共享 virtio/ramfb/DTB/HalVideo（HAL/Virt）；复用 x86 Video 绘制
INCLUDES_HAL += -IHAL/X64/Drivers -IHAL/Virt
VIRT_SRCS := $(wildcard HAL/Virt/*.c)
VIRT_SRCS += $(wildcard HAL/Virt/VirtioNet/*.c)
VIRT_OBJS := $(patsubst HAL/Virt/%.c,$(HALDIR)/Virt/%.o,$(VIRT_SRCS))
VIRT_VIDEO_SRCS := $(wildcard HAL/X64/Drivers/Video/*.c)
VIRT_VIDEO_OBJS := $(patsubst HAL/X64/Drivers/Video/%.c,$(HALDIR)/Drivers/Video/%.o,$(VIRT_VIDEO_SRCS))
EXTRA_OBJS += $(VIRT_OBJS) $(VIRT_VIDEO_OBJS)
ifeq ($(ARCH),riscv)
# RiscV virt MMIO 窗与 Arm 不同；Arm 用 VirtioMmio.c 内默认值
CFLAGS_HAL += -DVIRTIO_MMIO_BASE0=0x10001000ULL \
              -DVIRTIO_MMIO_STRIDE=0x1000u \
              -DVIRTIO_MMIO_COUNT=8u
endif
# PR-A12：本 arch 静态 HELLO.ELF（用户 VA @ 0x100000000）；放在 Arch 目录以免换架构互相覆盖
USER_VIRT_DIR = $(HALDIR)/user
ifeq ($(ARCH),arm64)
USER_LD = User/user-arm64.ld
USER_CRT0_SRC = User/crt/crt0_aarch64.S
USER_SYSCALL_SRC = User/crt/syscall_aarch64.S
USER_LDFLAGS = -z noexecstack
else
USER_LD = User/user-riscv.ld
USER_CRT0_SRC = User/crt/crt0_riscv.S
USER_SYSCALL_SRC = User/crt/syscall_riscv.S
USER_LDFLAGS = -m elf64lriscv -z noexecstack
endif
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic $(ARCH_CFLAGS) -IUser/include
USER_HELLO_ELF = $(USER_VIRT_DIR)/hello.elf
USER_HELLO_OBJ = $(USER_VIRT_DIR)/hello.o
USER_CRT_OBJS = $(USER_VIRT_DIR)/crt0.o $(USER_VIRT_DIR)/syscall.o \
	$(USER_VIRT_DIR)/string.o $(USER_VIRT_DIR)/printf.o \
	$(USER_VIRT_DIR)/malloc.o $(USER_VIRT_DIR)/errno.o \
	$(USER_VIRT_DIR)/unistd.o $(USER_VIRT_DIR)/stdlib.o \
	$(USER_VIRT_DIR)/signal.o $(USER_VIRT_DIR)/dirent.o \
	$(USER_VIRT_DIR)/stdio.o
endif

OBJS = $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(FONT_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS) $(LWIPOBJS) $(LWIP_PORT_OBJS)
TARGET = $(HALDIR)/Kernel.elf

ifeq ($(BRINGUP),1)
# PR-A6：Startup.S + Startup.c + HalSerial + Hal（Halt），不链 Common
# PR-B2：仍链 Board.o（Startup 横幅 BoardName；HalSerial 用 BoardConfig）
OBJS = $(HALDIR)/Startup_asm.o \
       $(HALDIR)/Startup.o \
       $(patsubst HAL/$(HAL_ARCH)/Hal/HalSerial/%.c,$(HALDIR)/Hal/HalSerial/%.o,$(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)) \
       $(patsubst HAL/$(HAL_ARCH)/Hal/%.c,$(HALDIR)/Hal/%.o,$(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial.c)) \
       $(HALDIR)/Hal/Hal.o \
       $(BOARD_OBJS)
EXTRA_OBJS =
endif

# 汇编用同一 TOY_BRINGUP（Startup.S 无条件调 StartupMain）
ASFLAGS_ARCH = -DTOY_BRINGUP=$(BRINGUP)

.PHONY: all clean boards kernel-bin
.DEFAULT_GOAL := all

all: $(TARGET)
ifneq ($(ARCH),x86_64)
	@echo "Built BOARD=$(BOARD) ($(BOARD_DIR))"
endif

# PR-B3：扁平二进制（U-Boot load / go；非 Linux Image 头）
kernel-bin: $(TARGET)
ifeq ($(ARCH),x86_64)
	$(error kernel-bin: x86 uses ELF via ToyBoot; not applicable)
endif
	$(OBJCOPY) -O binary $(TARGET) $(HALDIR)/Kernel.bin
	@echo "Wrote $(HALDIR)/Kernel.bin (BOARD=$(BOARD))"
ifeq ($(ARCH),x86_64)
ifneq ($(BRINGUP),1)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF) \
	$(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF) \
	$(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF) \
	$(USER_EXECDEMO_ELF) $(USER_PIPEDEMO_ELF) $(USER_BRKDEMO_ELF) $(USER_MMAPDEMO_ELF) $(USER_KILLDEMO_ELF) \
	$(USER_SIGDEMO_ELF) \
	$(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_DIRDEMO_ELF) \
	$(USER_NETLIB_ELF) $(USER_LIB_TOYOS_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A)
endif
else
ifneq ($(BRINGUP),1)
all: $(USER_HELLO_ELF)
endif
endif

# PR-B2：列出当前 Arch 可用板包
boards:
ifeq ($(ARCH),x86_64)
	@echo "BOARD: (unused on x86_64; desktop PC is 1.3c / HAL/X64)"
else
	@echo "ARCH=$(ARCH) HAL_ARCH=$(HAL_ARCH) BOARD=$(BOARD) → $(BOARD_DIR)"
	@ls -1 HAL/$(HAL_ARCH)/Board 2>/dev/null | sed 's/^/  /' || echo "  (none)"
endif

$(BUILDDIR) $(HALDIR):
	mkdir -p $@

$(TARGET): $(OBJS) | $(HALDIR)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)
ifneq ($(ARCH),x86_64)
	@echo "$(BOARD)" > $(HALDIR)/.toy_board
endif

$(BUILDDIR)/Common/Core/%.o: Common/Core/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Common/Services/%.o: Common/Services/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Common/Library/%.o: Common/Library/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Common/Fonts/%.o: Common/Fonts/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(HALDIR)/Drivers/%.o: HAL/$(HAL_ARCH)/Drivers/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

# 先于通用 HAL/%.o：依赖 DEMO_STAMP，换 TOY_DEMO_DRIVER=0/1 会触发重编+重链
$(HALDIR)/Hal/HalDevices.o: HAL/$(HAL_ARCH)/Hal/HalDevices.c $(DEMO_STAMP) | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

ifneq ($(ARCH),x86_64)
$(HALDIR)/Drivers/Video/%.o: HAL/X64/Drivers/Video/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/Virt/%.o: HAL/Virt/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

# PR-B2：HAL/<Arch>/Board/<board>/*.c
$(HALDIR)/Board/%.o: $(BOARD_DIR)/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(HALDIR)/%.o: HAL/$(HAL_ARCH)/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/LwIp/%.o: HAL/$(HAL_ARCH)/LwIp/%.c | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(LWIP),1)
$(BUILDDIR)/ThirdParty/lwip/src/%.o: $(LWIPDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(HALDIR)/%.o: HAL/$(HAL_ARCH)/%.S | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

# PR-A6：Startup.S 与 Startup.c 同名冲突，汇编产出 Startup_asm.o
$(HALDIR)/Startup_asm.o: HAL/$(HAL_ARCH)/Startup.S | $(HALDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(HALDIR)/SmpTramp.bin: HAL/X64/SmpTrampoline.S HAL/X64/SmpTrampoline.ld | $(HALDIR)
	$(CC) -c HAL/X64/SmpTrampoline.S -o $(HALDIR)/SmpTrampoline_low.o
	$(LD) -z noexecstack -T HAL/X64/SmpTrampoline.ld -o $(HALDIR)/SmpTrampoline_low.elf \
		$(HALDIR)/SmpTrampoline_low.o
	objcopy -O binary $(HALDIR)/SmpTrampoline_low.elf $@

$(HALDIR)/SmpTramp_blob.o: $(HALDIR)/SmpTramp.bin
	cd $(HALDIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		SmpTramp.bin SmpTramp_blob.o

$(USER_HELLO_OBJ): User/Apps/Hello.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/stddef.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Hello.c -o $@

User/crt/%.o: User/crt/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

User/crt/%.o: User/crt/%.S
	$(CC) -c $< -o $@

$(USER_COUNT_OBJ): User/Apps/Count.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS)

$(USER_EXECDEMO_OBJ): User/Apps/ExecDemo.c User/include/stdio.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/ExecDemo.c -o $@

$(USER_EXECDEMO_ELF): $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_PIPEDEMO_OBJ): User/Apps/PipeDemo.c User/include/stdio.h User/include/unistd.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/PipeDemo.c -o $@

$(USER_PIPEDEMO_ELF): $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_BRKDEMO_OBJ): User/Apps/BrkDemo.c User/include/stdio.h User/include/stdlib.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/BrkDemo.c -o $@

$(USER_BRKDEMO_ELF): $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_MMAPDEMO_OBJ): User/Apps/MmapDemo.c User/include/stdio.h User/include/string.h User/include/sys/mman.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/MmapDemo.c -o $@

$(USER_MMAPDEMO_ELF): $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_KILLDEMO_OBJ): User/Apps/KillDemo.c User/include/stdio.h User/include/unistd.h User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/KillDemo.c -o $@

$(USER_KILLDEMO_ELF): $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SIGDEMO_OBJ): User/Apps/SigDemo.c User/include/stdio.h User/include/stdlib.h User/include/unistd.h User/include/signal.h User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/SigDemo.c -o $@

$(USER_SIGDEMO_ELF): $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_WINDEMO_OBJ): User/Apps/WinDemo.c User/include/stdio.h User/include/unistd.h User/include/ToySyscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/WinDemo.c -o $@

$(USER_WINDEMO_ELF): $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_LIB_TOY_GFX_OBJ): User/Library/ToyGfx/ToyGfx.c User/include/ToyGfx.h User/include/unistd.h User/include/ToySyscall.h 
	$(CC) $(USER_CFLAGS) -c User/Library/ToyGfx/ToyGfx.c -o $@

$(USER_LIB_TOY_UI_OBJ): User/Library/ToyUi/ToyUi.c User/Library/ToyUi/ToyUiPrivate.h \
		User/include/ToyUi.h User/include/ToyGfx.h User/include/unistd.h \
		User/include/ToySyscall.h
	$(CC) $(USER_CFLAGS) -c User/Library/ToyUi/ToyUi.c -o $@

$(USER_LIB_TOY_UI_WIDGETS_OBJ): User/Library/ToyUi/ToyUiWidgets.c \
		User/Library/ToyUi/ToyUiPrivate.h User/include/ToyUi.h User/include/ToyGfx.h
	$(CC) $(USER_CFLAGS) -c User/Library/ToyUi/ToyUiWidgets.c -o $@

$(USER_LIB_TOY_GFX_A): $(USER_LIB_TOY_GFX_OBJ)
	ar rcs $@ $(USER_LIB_TOY_GFX_OBJ)

$(USER_LIB_TOY_UI_A): $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ)
	ar rcs $@ $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ)

$(USER_LIB_TOY_NET_OBJ): User/Library/ToyNet/ToyNet.c User/include/ToyNet.h \
		User/include/unistd.h User/include/errno.h User/include/string.h \
		User/include/toyos/syscall.h
	$(CC) $(USER_CFLAGS) -c User/Library/ToyNet/ToyNet.c -o $@

$(USER_LIB_TOY_NET_A): $(USER_LIB_TOY_NET_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_TOY_NET_OBJ)

$(USER_LIB_FSUTIL_OBJ): User/Library/FsUtil/FsUtil.c User/include/FsUtil.h User/include/dirent.h User/include/errno.h 
	$(CC) $(USER_CFLAGS) -c User/Library/FsUtil/FsUtil.c -o $@

$(USER_LIB_FSUTIL_A): $(USER_LIB_FSUTIL_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_FSUTIL_OBJ)

# PR-L1：CRT C 部分打成 libtoyos.a，供 User/Pkg 课外链接
$(USER_LIB_TOYOS_A): $(USER_LIB_TOYOS_OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_TOYOS_OBJS)

$(USER_GUIDEMO_OBJ): User/Apps/GuiDemo.c User/include/ToyUi.h User/include/ToyGfx.h User/include/stdio.h User/include/unistd.h User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/GuiDemo.c -o $@

$(USER_GUIDEMO_ELF): $(USER_GUIDEMO_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_GUIDEMO_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_BLITDEMO_OBJ): User/Apps/BlitDemo.c User/include/ToyUi.h User/include/ToyGfx.h User/include/stdio.h User/include/unistd.h User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/BlitDemo.c -o $@

$(USER_BLITDEMO_ELF): $(USER_BLITDEMO_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BLITDEMO_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_LIBCDEMO_OBJ): User/Apps/LibcDemo.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/LibcDemo.c -o $@

$(USER_LIBCDEMO_ELF): $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_DIRDEMO_OBJ): User/Apps/DirDemo.c User/include/stdio.h User/include/dirent.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/DirDemo.c -o $@

$(USER_DIRDEMO_ELF): $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_NETLIB_OBJ): User/Apps/NetLibDemo.c User/include/ToyNet.h User/include/stdio.h User/include/string.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/NetLibDemo.c -o $@

$(USER_NETLIB_ELF): $(USER_NETLIB_OBJ) $(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETLIB_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS)

$(USER_COUNT_ELF): $(USER_COUNT_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_COUNT_OBJ)

$(USER_FORK_OBJ): User/Apps/Fork.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_WAITNH_OBJ): User/Apps/WaitNoHang.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_LIBTOY_OBJ): User/Apps/LibToy.S | $(USER_OUT)
	$(CC) -fPIC -c $< -o $@

$(USER_DYNDEMO_OBJ): User/Apps/DynDemo.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_CAT_OBJ): User/Apps/Cat.c User/include/unistd.h User/include/fcntl.h User/include/errno.h User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Cat.c -o $@

$(USER_WRITE_OBJ): User/Apps/WriteFile.c User/include/unistd.h User/include/fcntl.h User/include/errno.h User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/WriteFile.c -o $@

$(USER_NETDEMO_OBJ): User/Apps/NetDemo.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_NETSRV_OBJ): User/Apps/NetServer.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_SYSHELLO_OBJ): User/Apps/SysHello.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_SYSFORK_OBJ): User/Apps/SysFork.S | $(USER_OUT)
	$(CC) -c $< -o $@

$(USER_FORK_ELF): $(USER_FORK_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_FORK_OBJ)

$(USER_WAITNH_ELF): $(USER_WAITNH_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WAITNH_OBJ)

$(USER_LIBTOY_SO): $(USER_LIBTOY_OBJ) | $(USER_OUT)
	$(LD) -shared -z noexecstack -soname LIBTOY.SO -o $@ $(USER_LIBTOY_OBJ)

$(USER_DYNDEMO_ELF): $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO) | $(USER_OUT)
	$(LD) -nostdlib -no-pie -z noexecstack -Ttext-segment=0x40000000 -z max-page-size=0x1000 \
		-o $@ $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO)

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CAT_OBJ) $(USER_CRT_OBJS)

$(USER_WRITE_ELF): $(USER_WRITE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WRITE_OBJ) $(USER_CRT_OBJS)

$(USER_NETDEMO_ELF): $(USER_NETDEMO_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETDEMO_OBJ)

$(USER_NETSRV_ELF): $(USER_NETSRV_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETSRV_OBJ)

$(USER_SYSHELLO_ELF): $(USER_SYSHELLO_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SYSHELLO_OBJ)

$(USER_SYSFORK_ELF): $(USER_SYSFORK_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SYSFORK_OBJ)

$(USER_OUT):
	mkdir -p $@

$(HALDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(HALDIR)
	# 固定输入名为 User_hello.elf，保留 `_binary_User_hello_elf_*`（Process.c / HelloBlob.c）
	cp -f $(USER_HELLO_ELF) $(HALDIR)/User_hello.elf
	cd $(HALDIR) && objcopy -I binary -O $(USER_BLOB_FMT) User_hello.elf User_hello_blob.o
endif

ifneq ($(ARCH),x86_64)
ifneq ($(BRINGUP),1)
$(USER_VIRT_DIR):
	mkdir -p $(USER_VIRT_DIR)

$(USER_HELLO_OBJ): User/Apps/Hello.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/stddef.h | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/Apps/Hello.c -o $@

$(USER_VIRT_DIR)/crt0.o: $(USER_CRT0_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_CRT0_SRC) -o $@

$(USER_VIRT_DIR)/syscall.o: $(USER_SYSCALL_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_SYSCALL_SRC) -o $@

$(USER_VIRT_DIR)/string.o: User/crt/string.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/string.c -o $@

$(USER_VIRT_DIR)/printf.o: User/crt/printf.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/printf.c -o $@

$(USER_VIRT_DIR)/malloc.o: User/crt/malloc.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/malloc.c -o $@

$(USER_VIRT_DIR)/errno.o: User/crt/errno.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/errno.c -o $@

$(USER_VIRT_DIR)/unistd.o: User/crt/unistd.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/unistd.c -o $@

$(USER_VIRT_DIR)/stdlib.o: User/crt/stdlib.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/stdlib.c -o $@

$(USER_VIRT_DIR)/signal.o: User/crt/signal.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/signal.c -o $@

$(USER_VIRT_DIR)/dirent.o: User/crt/dirent.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/dirent.c -o $@

$(USER_VIRT_DIR)/stdio.o: User/crt/stdio.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/stdio.c -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_VIRT_DIR)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS)
endif
endif

clean:
	# 只清当前 Arch 的 HAL 产物；共享 Common/Fonts/.o 必须清（随 ARCH 重编）
	rm -rf $(HALDIR)
	rm -rf $(BUILDDIR)/Common $(BUILDDIR)/Fonts $(BUILDDIR)/ThirdParty/lwip $(BUILDDIR)/lwip
	# 旧布局残留
	rm -rf Build/arm64 Build/riscv
	rm -f Build/Kernel.elf Build/SmpTramp.bin Build/SmpTramp_blob.o \
		Build/SmpTrampoline_low.elf Build/SmpTrampoline_low.o Build/User_hello_blob.o
ifeq ($(ARCH),x86_64)
	rm -rf $(USER_OUT)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_FORK_OBJ) $(USER_WAITNH_OBJ)
	rm -f $(USER_LIBTOY_OBJ) $(USER_DYNDEMO_OBJ) $(USER_CAT_OBJ) $(USER_WRITE_OBJ)
	rm -f $(USER_NETDEMO_OBJ) $(USER_NETSRV_OBJ) $(USER_SYSHELLO_OBJ) $(USER_SYSFORK_OBJ)
	rm -f $(USER_EXECDEMO_OBJ) $(USER_PIPEDEMO_OBJ) $(USER_BRKDEMO_OBJ) $(USER_MMAPDEMO_OBJ) $(USER_KILLDEMO_OBJ) $(USER_SIGDEMO_OBJ)
	rm -f $(USER_WINDEMO_OBJ) $(USER_GUIDEMO_OBJ) $(USER_BLITDEMO_OBJ) $(USER_LIBCDEMO_OBJ) $(USER_DIRDEMO_OBJ)
	rm -f $(USER_NETLIB_OBJ)
	rm -f $(USER_LIB_TOY_GFX_OBJ) $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ) \
		$(USER_LIB_TOY_NET_OBJ) $(USER_LIB_FSUTIL_OBJ)
	rm -f $(USER_LIB_TOY_GFX_A) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A) $(USER_LIB_TOYOS_A)
	rm -f $(USER_CRT_OBJS)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF)
	rm -f $(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
	rm -f $(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF)
	rm -f $(USER_EXECDEMO_ELF) $(USER_PIPEDEMO_ELF) $(USER_BRKDEMO_ELF) $(USER_MMAPDEMO_ELF) $(USER_KILLDEMO_ELF) $(USER_SIGDEMO_ELF)
	rm -f $(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_DIRDEMO_ELF)
	rm -f $(USER_NETLIB_ELF)
endif
