ARCH ?= x86_64
# PR-B2：Arm/RiscV 板包选择 → CodeA-HAL/<Arch>/Board/<board>/；x86 桌面真机走 1.3c，忽略 BOARD
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
SCREEN_LOG ?= 0
# SCREEN_LOG=1 时的分模块默认；总关时下方 ifeq 清零
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
                  -IInclude/Abi \
                  -IInclude/Hal \
                  -IInclude/Core \
                  -IInclude/Driver \
                  -IInclude/Library \
                  -IInclude/Services \
                  -ICodeB-Library \
                  -ICodeB-Library/Fonts \
                  -ICodeA-HAL/$(HAL_ARCH) \
                  -ICodeA-HAL/$(HAL_ARCH)/Hal
INCLUDES_HAL    = $(INCLUDES_COMMON) \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/XHCI \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Ehci \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Uhci \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Ps2 \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Ata \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Msc \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Net \
                  -ICodeA-HAL/$(HAL_ARCH)/Drivers/Iwl

# PR-B2：非 x86 必须选中 Board 包（默认 virt）；Common 不 -I 板目录
ifeq ($(ARCH),x86_64)
BOARD_DIR :=
else
BOARD_DIR := CodeA-HAL/$(HAL_ARCH)/Board/$(BOARD)
ifeq ($(wildcard $(BOARD_DIR)/README.md),)
$(error BOARD=$(BOARD): missing $(BOARD_DIR)/ (see CodeA-HAL/Board/README.md); default BOARD=virt)
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
               -ICodeA-HAL/$(HAL_ARCH)/LwIp/include \
               -ICodeA-HAL/$(HAL_ARCH)/LwIp
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
LWIP_PORT_SRCS = CodeA-HAL/$(HAL_ARCH)/LwIp/toy_netif.c \
                 CodeA-HAL/$(HAL_ARCH)/LwIp/toy_ping.c \
                 CodeA-HAL/$(HAL_ARCH)/LwIp/toy_tcpecho.c \
                 CodeA-HAL/$(HAL_ARCH)/LwIp/toy_udp.c \
                 CodeA-HAL/$(HAL_ARCH)/LwIp/toy_tcpclient.c \
                 CodeA-HAL/$(HAL_ARCH)/LwIp/toy_socket.c
LWIP_PORT_OBJS = $(patsubst CodeA-HAL/$(HAL_ARCH)/LwIp/%.c,$(HALDIR)/LwIp/%.o,$(LWIP_PORT_SRCS))
endif

CFLAGS_COMMON = $(CFLAGS_BASE) $(INCLUDES_COMMON) $(LWIPINCLUDES)
CFLAGS_HAL    = $(CFLAGS_BASE) $(INCLUDES_HAL) $(LWIPINCLUDES) -ICodeA-HAL/$(HAL_ARCH)/LwIp
ifneq ($(BOARD_DIR),)
CFLAGS_HAL += -DTOY_BOARD=\"$(BOARD)\"
endif

LDFLAGS = -nostdlib -static -z noexecstack -T CodeA-HAL/$(HAL_ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)
# SpinLock 的 __sync_* 需要 libgcc（如 __aarch64_swp4_sync）
LIBGCC := $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

# 产物树：Common、Services、Library、Core、User、Fonts 与 HAL 同级；HAL 下按 Arch 分目录。
# 有 ToyOS 树时由 build.sh 传入 BUILDDIR=$TOYOS_ROOT/Build/ToyKernel；单仓默认 ./Build。
# 各 Arch 的 Kernel.elf / HAL .o 留在 $(BUILDDIR)/CodeA-HAL/<Arch>/，换架构不删其它 Arch 成品。
BUILDDIR ?= Build
HALDIR = $(BUILDDIR)/CodeA-HAL/$(HAL_ARCH)

# Common / Services / Library / Core / User / Fonts 的 .o 跨 Arch 共用路径：换 ARCH 时若仍用旧 .o 会链错格式
# （如 EM:62 x86_64 → aarch64/riscv）。勿只清 Common。
ARCH_STAMP := $(BUILDDIR)/.toy_arch
_STAMP_ARCH := $(shell cat $(ARCH_STAMP) 2>/dev/null)
ifneq ($(_STAMP_ARCH),$(ARCH))
$(info ARCH: stale '$(_STAMP_ARCH)' → '$(ARCH)'; cleaning $(BUILDDIR)/{CodeA–E,lwip})
$(shell rm -rf '$(BUILDDIR)/CodeA-HAL' '$(BUILDDIR)/CodeB-Library' \
	'$(BUILDDIR)/CodeC-Core' '$(BUILDDIR)/CodeC-Modules' '$(BUILDDIR)/CodeD-Services' '$(BUILDDIR)/CodeE-User' \
	'$(BUILDDIR)/ThirdParty/lwip' '$(BUILDDIR)/lwip')
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

CORE_SRCS     := $(wildcard CodeC-Core/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/Kernel/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/Scheduler/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/Process/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/TaskFd/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/Syscall/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/VirtualMemory/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/PhysicalMemory/*.c)
CORE_SRCS     += $(wildcard CodeC-Core/Device/*.c)
SERVICES_SRCS := $(wildcard CodeD-Services/*.c)
# CodeD-Services/*.c 不进子目录；每个模块开目录时补一行
SERVICES_SRCS += $(wildcard CodeD-Services/Locale/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiDrag/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiDraw/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiPointer/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiResize/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiUser/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiOpen/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiBackup/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiFocus/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Console/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Desktop/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/FilesUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Store/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/SettingsUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Theme/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/StoreUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/DevicesUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/EditUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/TtyUi/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/StoreNet/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Db/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/FileSystem/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Tasks/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/ShellCommands/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/Tcp/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/LwIp/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiCompose/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiCursor/*.c)
SERVICES_SRCS += $(wildcard CodeD-Services/GuiWm/*.c)
LIB_SRCS      := $(wildcard CodeB-Library/*.c)
LIB_SRCS      += $(wildcard CodeB-Library/Gpt/*.c)
LIB_SRCS      += $(wildcard CodeB-Library/Elf/*.c)
LIB_SRCS      += $(wildcard CodeB-Library/UI/*.c)
FONT_SRCS     := $(wildcard CodeB-Library/Fonts/*.c)
DRIVER_SRCS   := $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Video/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Net/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/VirtioNet/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/E1000/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Alx/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Rtl/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Wifi/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Iwl/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Iwl/*/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Ehci/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Uhci/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Ps2/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Nvme/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Ahci/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Ata/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Msc/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Igpu/*.c)
DRIVER_SRCS   += $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/Hda/*.c)
# PR-H-xhci-split-8：Drivers/XHCI/*.c（CodeC-Core/Port/Device/Hid/Hub/Mouse/Irq/Diag）；已删单体 Drivers/XHCI.c
XHCI_SPLIT_SRCS := $(wildcard CodeA-HAL/$(HAL_ARCH)/Drivers/XHCI/*.c)
DRIVER_SRCS   += $(XHCI_SPLIT_SRCS)
ARCH_SRCS     := $(wildcard CodeA-HAL/$(HAL_ARCH)/*.c)
ARCH_SRCS     += $(wildcard CodeA-HAL/$(HAL_ARCH)/Hal/*.c)
ARCH_SRCS     += $(wildcard CodeA-HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)
ARCH_SRCS     += $(wildcard CodeA-HAL/$(HAL_ARCH)/AcpiMadt/*.c)
ARCH_ASM_ALL  := $(wildcard CodeA-HAL/$(HAL_ARCH)/*.S)
ARCH_ASM      := $(filter-out CodeA-HAL/$(HAL_ARCH)/SmpTrampoline.S CodeA-HAL/$(HAL_ARCH)/Startup.S,$(ARCH_ASM_ALL))

CORE_OBJS     := $(patsubst CodeC-Core/%.c,$(BUILDDIR)/CodeC-Core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst CodeD-Services/%.c,$(BUILDDIR)/CodeD-Services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst CodeB-Library/%.c,$(BUILDDIR)/CodeB-Library/%.o,$(LIB_SRCS))
FONT_OBJS     := $(patsubst CodeB-Library/Fonts/%.c,$(BUILDDIR)/CodeB-Library/Fonts/%.o,$(FONT_SRCS))
DRIVER_OBJS   := $(patsubst CodeA-HAL/$(HAL_ARCH)/Drivers/%.c,$(HALDIR)/Drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst CodeA-HAL/$(HAL_ARCH)/%.c,$(HALDIR)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst CodeA-HAL/$(HAL_ARCH)/%.S,$(HALDIR)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(HALDIR)/User_hello_blob.o $(HALDIR)/SmpTramp_blob.o
# 演示 ELF/OBJ 进 Build/CodeE-User/（源在 CodeE-User/Apps/）；CRT/Library 仍就地编
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
USER_SLEEPDEMO_ELF = $(USER_OUT)/sleepdemo.elf
USER_THREADSMOKE_ELF = $(USER_OUT)/threadsmoke.elf
USER_PTHREADSMOKE_ELF = $(USER_OUT)/pthreadsmoke.elf
USER_THREADDEMO_ELF = $(USER_OUT)/threaddemo.elf
USER_SNAKE_ELF = $(USER_OUT)/snake.elf
USER_TASKMGR_ELF = $(USER_OUT)/taskmgr.elf
USER_DIRDEMO_ELF = $(USER_OUT)/dirdemo.elf
USER_CWDDEMO_ELF = $(USER_OUT)/cwddemo.elf
USER_NETLIB_ELF = $(USER_OUT)/netlibdemo.elf
USER_SOCKDEMO_ELF = $(USER_OUT)/sockdemo.elf
USER_CHAT_ELF = $(USER_OUT)/chat.elf
USER_ENOSYS_ELF = $(USER_OUT)/enosysdemo.elf
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
USER_SLEEPDEMO_OBJ = $(USER_OUT)/sleepdemo.o
USER_THREADSMOKE_OBJ = $(USER_OUT)/threadsmoke.o
USER_PTHREADSMOKE_OBJ = $(USER_OUT)/pthreadsmoke.o
USER_THREADDEMO_OBJ = $(USER_OUT)/threaddemo.o
USER_SNAKE_OBJ = $(USER_OUT)/snake.o
USER_TASKMGR_OBJ = $(USER_OUT)/taskmgr.o
USER_DIRDEMO_OBJ = $(USER_OUT)/dirdemo.o
USER_CWDDEMO_OBJ = $(USER_OUT)/cwddemo.o
USER_NETLIB_OBJ = $(USER_OUT)/netlibdemo.o
USER_SOCKDEMO_OBJ = $(USER_OUT)/sockdemo.o
USER_CHAT_OBJ = $(USER_OUT)/chat.o
USER_ENOSYS_OBJ = $(USER_OUT)/enosysdemo.o
USER_LIB_TOY_GFX_OBJ = CodeE-User/Library/ToyGfx/ToyGfx.o
USER_LIB_TOY_UI_OBJ = CodeE-User/Library/ToyUi/ToyUi.o
USER_LIB_TOY_UI_WIDGETS_OBJ = CodeE-User/Library/ToyUi/ToyUiWidgets.o
USER_LIB_TOY_NET_OBJ = CodeE-User/Library/ToyNet/ToyNet.o
USER_LIB_FSUTIL_OBJ = CodeE-User/Library/FsUtil/FsUtil.o
USER_LIB_TOY_GFX_A = CodeE-User/Library/ToyGfx/libToyGfx.a
USER_LIB_TOY_UI_A = CodeE-User/Library/ToyUi/libToyUi.a
USER_LIB_TOY_NET_A = CodeE-User/Library/ToyNet/libToyNet.a
USER_LIB_FSUTIL_A = CodeE-User/Library/FsUtil/libFsUtil.a
USER_LIB_TOYOS_A = CodeE-User/Library/ToyOs/libtoyos.a
USER_LIB_TOYOS_OBJS = CodeE-User/crt/string.o CodeE-User/crt/printf.o CodeE-User/crt/malloc.o \
	CodeE-User/crt/errno.o CodeE-User/crt/unistd.o CodeE-User/crt/sleep.o CodeE-User/crt/stdlib.o CodeE-User/crt/signal.o \
	CodeE-User/crt/dirent.o CodeE-User/crt/stdio.o CodeE-User/crt/socket.o CodeE-User/crt/cwd.o CodeE-User/crt/sched.o \
	CodeE-User/crt/proc.o CodeE-User/crt/stat.o CodeE-User/crt/thread_root.o CodeE-User/crt/pthread.o
USER_LD = CodeE-User/user.ld
USER_LDFLAGS = -z noexecstack
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone -ICodeE-User/include -IInclude -IInclude/Abi
USER_ASFLAGS = -ICodeE-User/include -IInclude -IInclude/Abi
USER_CRT_OBJS = CodeE-User/crt/crt0.o CodeE-User/crt/syscall.o $(USER_LIB_TOYOS_OBJS)
else
EXTRA_OBJS = $(HALDIR)/Startup_asm.o
# PR-B2：Board 包 .c（BoardConfig.h 仅 HAL -I；不进 Common）
BOARD_SRCS := $(wildcard $(BOARD_DIR)/*.c)
BOARD_OBJS := $(patsubst $(BOARD_DIR)/%.c,$(HALDIR)/Board/%.o,$(BOARD_SRCS))
EXTRA_OBJS += $(BOARD_OBJS)
# PR-R5：Arm/RiscV 共享 virtio/ramfb/DTB/HalVideo（CodeA-HAL/Virt）；复用 x86 Video 绘制
INCLUDES_HAL += -ICodeA-HAL/X64/Drivers -ICodeA-HAL/Virt
VIRT_SRCS := $(wildcard CodeA-HAL/Virt/*.c)
VIRT_SRCS += $(wildcard CodeA-HAL/Virt/VirtioNet/*.c)
VIRT_OBJS := $(patsubst CodeA-HAL/Virt/%.c,$(HALDIR)/Virt/%.o,$(VIRT_SRCS))
VIRT_VIDEO_SRCS := $(wildcard CodeA-HAL/X64/Drivers/Video/*.c)
VIRT_VIDEO_OBJS := $(patsubst CodeA-HAL/X64/Drivers/Video/%.c,$(HALDIR)/Drivers/Video/%.o,$(VIRT_VIDEO_SRCS))
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
USER_LD = CodeE-User/user-arm64.ld
USER_CRT0_SRC = CodeE-User/crt/crt0_aarch64.S
USER_SYSCALL_SRC = CodeE-User/crt/syscall_aarch64.S
USER_LDFLAGS = -z noexecstack
else
USER_LD = CodeE-User/user-riscv.ld
USER_CRT0_SRC = CodeE-User/crt/crt0_riscv.S
USER_SYSCALL_SRC = CodeE-User/crt/syscall_riscv.S
USER_LDFLAGS = -m elf64lriscv -z noexecstack
endif
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic $(ARCH_CFLAGS) -ICodeE-User/include -IInclude -IInclude/Abi
ifeq ($(ARCH),arm64)
# pthread __sync_*：内联原子，避免裸链依赖 __aarch64_swp4_sync（libgcc）
USER_CFLAGS += -mno-outline-atomics
endif
USER_HELLO_ELF = $(USER_VIRT_DIR)/hello.elf
USER_HELLO_OBJ = $(USER_VIRT_DIR)/hello.o
USER_CRT_OBJS = $(USER_VIRT_DIR)/crt0.o $(USER_VIRT_DIR)/syscall.o \
	$(USER_VIRT_DIR)/string.o $(USER_VIRT_DIR)/printf.o \
	$(USER_VIRT_DIR)/malloc.o $(USER_VIRT_DIR)/errno.o \
	$(USER_VIRT_DIR)/unistd.o $(USER_VIRT_DIR)/sleep.o $(USER_VIRT_DIR)/stdlib.o \
	$(USER_VIRT_DIR)/signal.o $(USER_VIRT_DIR)/dirent.o \
	$(USER_VIRT_DIR)/stdio.o $(USER_VIRT_DIR)/socket.o \
	$(USER_VIRT_DIR)/cwd.o $(USER_VIRT_DIR)/sched.o $(USER_VIRT_DIR)/proc.o \
	$(USER_VIRT_DIR)/stat.o $(USER_VIRT_DIR)/thread_root.o $(USER_VIRT_DIR)/pthread.o
endif

SCHEDULER ?= round-robin
ifeq ($(SCHEDULER),round-robin)
SCHED_SRCS := CodeC-Modules/SchedulerRoundRobin/SchedulerRoundRobin.c
else ifeq ($(SCHEDULER),priority)
SCHED_SRCS := CodeC-Modules/Student/SchedulerPriority/SchedulerPriority.c
CFLAGS_COMMON += -DTOY_SCHED_PRIORITY
else
$(error unknown SCHEDULER=$(SCHEDULER))
endif
SCHED_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SCHED_SRCS))

MEMORY ?= bitmap
ifeq ($(MEMORY),bitmap)
MEMORY_SRCS := CodeC-Modules/PhysicalMemoryBitmap/PhysicalMemoryBitmap.c
else ifeq ($(MEMORY),bestfit)
MEMORY_SRCS := CodeC-Modules/Student/PhysicalMemoryBestFit/PhysicalMemoryBestFit.c
CFLAGS_COMMON += -DTOY_MEM_BESTFIT
else
$(error Unknown MEMORY: $(MEMORY))
endif
MEMORY_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(MEMORY_SRCS))

FS ?= fat
ifeq ($(FS),fat)
FS_SRCS := CodeC-Modules/FileSystemFat/FatFsOps.c \
           $(wildcard CodeC-Modules/FileSystemFat/Fat/*.c) \
           CodeC-Modules/FileSystemFat/FatIo.c \
           CodeC-Modules/FileSystemFat/FatFormat.c
else ifeq ($(FS),ram)
FS_SRCS := CodeC-Modules/Student/FileSystemRam/FileSystemRam.c \
           CodeC-Modules/Student/FileSystemRam/FileSystemRamCompat.c
CFLAGS_COMMON += -DTOY_FS_RAM
else
$(error Unknown FS: $(FS))
endif
FS_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(FS_SRCS))

OBJS = $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(FONT_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS) $(SCHED_OBJS) $(MEMORY_OBJS) $(FS_OBJS) $(LWIPOBJS) $(LWIP_PORT_OBJS)
TARGET = $(HALDIR)/Kernel.elf

ifeq ($(BRINGUP),1)
# PR-A6：Startup.S + Startup.c + HalSerial + Hal（Halt），不链 Common
# PR-B2：仍链 Board.o（Startup 横幅 BoardName；HalSerial 用 BoardConfig）
OBJS = $(HALDIR)/Startup_asm.o \
       $(HALDIR)/Startup.o \
       $(patsubst CodeA-HAL/$(HAL_ARCH)/Hal/HalSerial/%.c,$(HALDIR)/Hal/HalSerial/%.o,$(wildcard CodeA-HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)) \
       $(patsubst CodeA-HAL/$(HAL_ARCH)/Hal/%.c,$(HALDIR)/Hal/%.o,$(wildcard CodeA-HAL/$(HAL_ARCH)/Hal/HalSerial.c)) \
       $(HALDIR)/Hal/Hal.o \
       $(BOARD_OBJS)
EXTRA_OBJS =
endif

# 汇编用同一 TOY_BRINGUP（Startup.S 无条件调 StartupMain）
ASFLAGS_ARCH = -DTOY_BRINGUP=$(BRINGUP)

.PHONY: all clean boards kernel-bin runtests runtests-memory runtests-fs scheduler
.DEFAULT_GOAL := all

HOSTCC ?= gcc
TEST_CFLAGS = -std=c11 -Wall -Wextra -DTOY_SCHED_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(SCHEDULER),priority)
TEST_CFLAGS += -DTOY_SCHED_PRIORITY
endif

MEM_TEST_CFLAGS = -std=c11 -Wall -Wextra -DTOY_MEM_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(MEMORY),bestfit)
MEM_TEST_CFLAGS += -DTOY_MEM_BESTFIT
endif

FS_TEST_CFLAGS = -std=c11 -Wall -Wextra -DTOY_FS_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(FS),ram)
FS_TEST_CFLAGS += -DTOY_FS_RAM
FS_TEST_POLICY := CodeC-Modules/Student/FileSystemRam/FileSystemRam.c
else
FS_TEST_POLICY := Tools/Tests/Stub/FsStub.c
endif

scheduler: runtests

runtests:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(TEST_CFLAGS) -c $(SCHED_SRCS) -o $(BUILDDIR)/Tests/policy.o
	$(HOSTCC) $(TEST_CFLAGS) -c CodeC-Core/Scheduler/SchedulerOps.c -o $(BUILDDIR)/Tests/ops.o
	$(HOSTCC) $(TEST_CFLAGS) -c Tools/Tests/Stub/SchedulerStub.c -o $(BUILDDIR)/Tests/stub.o
	$(HOSTCC) $(TEST_CFLAGS) -c Tools/Tests/TestScheduler.c -o $(BUILDDIR)/Tests/test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestScheduler $(BUILDDIR)/Tests/policy.o $(BUILDDIR)/Tests/ops.o $(BUILDDIR)/Tests/stub.o $(BUILDDIR)/Tests/test.o
	./$(BUILDDIR)/Tests/TestScheduler

runtests-memory:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c $(MEMORY_SRCS) -o $(BUILDDIR)/Tests/mem_policy.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c CodeC-Core/PhysicalMemory/PhysicalMemoryOps.c -o $(BUILDDIR)/Tests/mem_ops.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c Tools/Tests/Stub/MemoryStub.c -o $(BUILDDIR)/Tests/mem_stub.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c Tools/Tests/TestMemory.c -o $(BUILDDIR)/Tests/mem_test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestMemory $(BUILDDIR)/Tests/mem_policy.o $(BUILDDIR)/Tests/mem_ops.o $(BUILDDIR)/Tests/mem_stub.o $(BUILDDIR)/Tests/mem_test.o
	./$(BUILDDIR)/Tests/TestMemory

runtests-fs:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(FS_TEST_CFLAGS) -c CodeB-Library/Vfs.c -o $(BUILDDIR)/Tests/fs_vfs.o
	$(HOSTCC) $(FS_TEST_CFLAGS) -c $(FS_TEST_POLICY) -o $(BUILDDIR)/Tests/fs_policy.o
	$(HOSTCC) $(FS_TEST_CFLAGS) -c Tools/Tests/TestFs.c -o $(BUILDDIR)/Tests/fs_test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestFs $(BUILDDIR)/Tests/fs_vfs.o $(BUILDDIR)/Tests/fs_policy.o $(BUILDDIR)/Tests/fs_test.o
	./$(BUILDDIR)/Tests/TestFs

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
	$(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_SLEEPDEMO_ELF) $(USER_THREADSMOKE_ELF) $(USER_PTHREADSMOKE_ELF) $(USER_THREADDEMO_ELF) $(USER_SNAKE_ELF) $(USER_TASKMGR_ELF) $(USER_DIRDEMO_ELF) $(USER_CWDDEMO_ELF) \
	$(USER_NETLIB_ELF) $(USER_SOCKDEMO_ELF) $(USER_CHAT_ELF) $(USER_ENOSYS_ELF) $(USER_LIB_TOYOS_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A)
endif
else
ifneq ($(BRINGUP),1)
all: $(USER_HELLO_ELF)
endif
endif

# PR-B2：列出当前 Arch 可用板包
boards:
ifeq ($(ARCH),x86_64)
	@echo "BOARD: (unused on x86_64; desktop PC is 1.3c / CodeA-HAL/X64)"
else
	@echo "ARCH=$(ARCH) HAL_ARCH=$(HAL_ARCH) BOARD=$(BOARD) → $(BOARD_DIR)"
	@ls -1 CodeA-HAL/$(HAL_ARCH)/Board 2>/dev/null | sed 's/^/  /' || echo "  (none)"
endif

$(BUILDDIR) $(HALDIR):
	mkdir -p $@

$(TARGET): $(OBJS) | $(HALDIR)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)
ifneq ($(ARCH),x86_64)
	@echo "$(BOARD)" > $(HALDIR)/.toy_board
endif

$(BUILDDIR)/CodeC-Core/%.o: CodeC-Core/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/CodeC-Modules/%.o: CodeC-Modules/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/CodeD-Services/%.o: CodeD-Services/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/CodeB-Library/%.o: CodeB-Library/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/CodeB-Library/Fonts/%.o: CodeB-Library/Fonts/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(HALDIR)/Drivers/%.o: CodeA-HAL/$(HAL_ARCH)/Drivers/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

# 先于通用 CodeA-HAL/%.o：依赖 DEMO_STAMP，换 TOY_DEMO_DRIVER=0/1 会触发重编+重链
$(HALDIR)/Hal/HalDevices.o: CodeA-HAL/$(HAL_ARCH)/Hal/HalDevices.c $(DEMO_STAMP) | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifneq ($(ARCH),x86_64)
$(HALDIR)/Drivers/Video/%.o: CodeA-HAL/X64/Drivers/Video/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/Virt/%.o: CodeA-HAL/Virt/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

# PR-B2：CodeA-HAL/<Arch>/Board/<board>/*.c
$(HALDIR)/Board/%.o: $(BOARD_DIR)/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(HALDIR)/%.o: CodeA-HAL/$(HAL_ARCH)/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/LwIp/%.o: CodeA-HAL/$(HAL_ARCH)/LwIp/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(LWIP),1)
$(BUILDDIR)/ThirdParty/lwip/src/%.o: $(LWIPDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(HALDIR)/%.o: CodeA-HAL/$(HAL_ARCH)/%.S | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

# PR-A6：Startup.S 与 Startup.c 同名冲突，汇编产出 Startup_asm.o
$(HALDIR)/Startup_asm.o: CodeA-HAL/$(HAL_ARCH)/Startup.S | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(HALDIR)/SmpTramp.bin: CodeA-HAL/X64/SmpTrampoline.S CodeA-HAL/X64/SmpTrampoline.ld | $(HALDIR)
	$(CC) -c CodeA-HAL/X64/SmpTrampoline.S -o $(HALDIR)/SmpTrampoline_low.o
	$(LD) -z noexecstack -T CodeA-HAL/X64/SmpTrampoline.ld -o $(HALDIR)/SmpTrampoline_low.elf \
		$(HALDIR)/SmpTrampoline_low.o
	objcopy -O binary $(HALDIR)/SmpTrampoline_low.elf $@

$(HALDIR)/SmpTramp_blob.o: $(HALDIR)/SmpTramp.bin
	cd $(HALDIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		SmpTramp.bin SmpTramp_blob.o

$(USER_HELLO_OBJ): CodeE-User/Apps/Hello.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/string.h CodeE-User/include/stddef.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/Hello.c -o $@

CodeE-User/crt/%.o: CodeE-User/crt/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

CodeE-User/crt/%.o: CodeE-User/crt/%.S
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_COUNT_OBJ): CodeE-User/Apps/Count.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS)

$(USER_EXECDEMO_OBJ): CodeE-User/Apps/ExecDemo.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/ExecDemo.c -o $@

$(USER_EXECDEMO_ELF): $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_PIPEDEMO_OBJ): CodeE-User/Apps/PipeDemo.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/PipeDemo.c -o $@

$(USER_PIPEDEMO_ELF): $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_BRKDEMO_OBJ): CodeE-User/Apps/BrkDemo.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/BrkDemo.c -o $@

$(USER_BRKDEMO_ELF): $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_MMAPDEMO_OBJ): CodeE-User/Apps/MmapDemo.c CodeE-User/include/stdio.h CodeE-User/include/string.h CodeE-User/include/sys/mman.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/MmapDemo.c -o $@

$(USER_MMAPDEMO_ELF): $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_KILLDEMO_OBJ): CodeE-User/Apps/KillDemo.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/KillDemo.c -o $@

$(USER_KILLDEMO_ELF): $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SIGDEMO_OBJ): CodeE-User/Apps/SigDemo.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/unistd.h CodeE-User/include/signal.h CodeE-User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/SigDemo.c -o $@

$(USER_SIGDEMO_ELF): $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_WINDEMO_OBJ): CodeE-User/Apps/WinDemo.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/ToySyscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/WinDemo.c -o $@

$(USER_WINDEMO_ELF): $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_LIB_TOY_GFX_OBJ): CodeE-User/Library/ToyGfx/ToyGfx.c CodeE-User/include/ToyGfx.h CodeE-User/include/unistd.h CodeE-User/include/ToySyscall.h 
	$(CC) $(USER_CFLAGS) -c CodeE-User/Library/ToyGfx/ToyGfx.c -o $@

$(USER_LIB_TOY_UI_OBJ): CodeE-User/Library/ToyUi/ToyUi.c CodeE-User/Library/ToyUi/ToyUiPrivate.h \
		CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h CodeE-User/include/unistd.h \
		CodeE-User/include/ToySyscall.h
	$(CC) $(USER_CFLAGS) -c CodeE-User/Library/ToyUi/ToyUi.c -o $@

$(USER_LIB_TOY_UI_WIDGETS_OBJ): CodeE-User/Library/ToyUi/ToyUiWidgets.c \
		CodeE-User/Library/ToyUi/ToyUiPrivate.h CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h
	$(CC) $(USER_CFLAGS) -c CodeE-User/Library/ToyUi/ToyUiWidgets.c -o $@

$(USER_LIB_TOY_GFX_A): $(USER_LIB_TOY_GFX_OBJ)
	ar rcs $@ $(USER_LIB_TOY_GFX_OBJ)

$(USER_LIB_TOY_UI_A): $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ)
	ar rcs $@ $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ)

$(USER_LIB_TOY_NET_OBJ): CodeE-User/Library/ToyNet/ToyNet.c CodeE-User/include/ToyNet.h \
		CodeE-User/include/unistd.h CodeE-User/include/errno.h CodeE-User/include/string.h \
		CodeE-User/include/toyos/syscall.h
	$(CC) $(USER_CFLAGS) -c CodeE-User/Library/ToyNet/ToyNet.c -o $@

$(USER_LIB_TOY_NET_A): $(USER_LIB_TOY_NET_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_TOY_NET_OBJ)

$(USER_LIB_FSUTIL_OBJ): CodeE-User/Library/FsUtil/FsUtil.c CodeE-User/include/FsUtil.h CodeE-User/include/dirent.h CodeE-User/include/errno.h 
	$(CC) $(USER_CFLAGS) -c CodeE-User/Library/FsUtil/FsUtil.c -o $@

$(USER_LIB_FSUTIL_A): $(USER_LIB_FSUTIL_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_FSUTIL_OBJ)

# PR-L1：CRT C 部分打成 libtoyos.a，供 CodeE-User/Pkg 课外链接
$(USER_LIB_TOYOS_A): $(USER_LIB_TOYOS_OBJS)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_TOYOS_OBJS)

$(USER_GUIDEMO_OBJ): CodeE-User/Apps/GuiDemo.c CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/GuiDemo.c -o $@

$(USER_GUIDEMO_ELF): $(USER_GUIDEMO_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_GUIDEMO_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_BLITDEMO_OBJ): CodeE-User/Apps/BlitDemo.c CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/BlitDemo.c -o $@

$(USER_BLITDEMO_ELF): $(USER_BLITDEMO_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BLITDEMO_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_LIBCDEMO_OBJ): CodeE-User/Apps/LibcDemo.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/string.h CodeE-User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/LibcDemo.c -o $@

$(USER_LIBCDEMO_ELF): $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SLEEPDEMO_OBJ): CodeE-User/Apps/SleepDemo.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/SleepDemo.c -o $@

$(USER_SLEEPDEMO_ELF): $(USER_SLEEPDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SLEEPDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_THREADSMOKE_OBJ): CodeE-User/Apps/ThreadSmoke.c CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/toyos/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/ThreadSmoke.c -o $@
$(USER_THREADSMOKE_ELF): $(USER_THREADSMOKE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_THREADSMOKE_OBJ) $(USER_CRT_OBJS)

$(USER_PTHREADSMOKE_OBJ): CodeE-User/Apps/PthreadSmoke.c CodeE-User/include/pthread.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/toyos/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/PthreadSmoke.c -o $@
$(USER_PTHREADSMOKE_ELF): $(USER_PTHREADSMOKE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_PTHREADSMOKE_OBJ) $(USER_CRT_OBJS)

$(USER_THREADDEMO_OBJ): CodeE-User/Apps/ThreadDemo.c CodeE-User/include/pthread.h CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/unistd.h CodeE-User/include/sched.h CodeE-User/include/toyos/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/ThreadDemo.c -o $@
$(USER_THREADDEMO_ELF): $(USER_THREADDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_THREADDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SNAKE_OBJ): CodeE-User/Apps/Snake.c CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/Snake.c -o $@

$(USER_SNAKE_ELF): $(USER_SNAKE_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SNAKE_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_TASKMGR_OBJ): CodeE-User/Apps/TaskMgr.c CodeE-User/include/ToyUi.h CodeE-User/include/toyos/task.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h CodeE-User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/TaskMgr.c -o $@

$(USER_TASKMGR_ELF): $(USER_TASKMGR_OBJ) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_TASKMGR_OBJ) \
		$(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_DIRDEMO_OBJ): CodeE-User/Apps/DirDemo.c CodeE-User/include/stdio.h CodeE-User/include/dirent.h CodeE-User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/DirDemo.c -o $@

$(USER_DIRDEMO_ELF): $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_CWDDEMO_OBJ): CodeE-User/Apps/CwdDemo.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/CwdDemo.c -o $@

$(USER_CWDDEMO_ELF): $(USER_CWDDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CWDDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_NETLIB_OBJ): CodeE-User/Apps/NetLibDemo.c CodeE-User/include/ToyNet.h CodeE-User/include/stdio.h CodeE-User/include/string.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/NetLibDemo.c -o $@

$(USER_NETLIB_ELF): $(USER_NETLIB_OBJ) $(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETLIB_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS)

$(USER_SOCKDEMO_OBJ): CodeE-User/Apps/SockDemo.c CodeE-User/include/sys/socket.h CodeE-User/include/ToyNet.h CodeE-User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/SockDemo.c -o $@

$(USER_SOCKDEMO_ELF): $(USER_SOCKDEMO_OBJ) $(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SOCKDEMO_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS)

$(USER_CHAT_OBJ): CodeE-User/Apps/Chat.c CodeE-User/Apps/ChatNet.h CodeE-User/include/ToyNet.h CodeE-User/include/ToyUi.h CodeE-User/include/ToyGfx.h CodeE-User/include/stdio.h CodeE-User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/Chat.c -o $@

USER_CHAT_NET_OBJ = $(USER_OUT)/chat_net.o
$(USER_CHAT_NET_OBJ): CodeE-User/Apps/ChatNet.c CodeE-User/Apps/ChatNet.h CodeE-User/include/ToyNet.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/ChatNet.c -o $@

$(USER_CHAT_ELF): $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) $(USER_LIB_TOY_NET_A) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_GFX_A) $(USER_CRT_OBJS)

$(USER_ENOSYS_OBJ): CodeE-User/Apps/EnosysDemo.c CodeE-User/include/stdio.h CodeE-User/include/errno.h CodeE-User/include/toyos/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/EnosysDemo.c -o $@

$(USER_ENOSYS_ELF): $(USER_ENOSYS_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_ENOSYS_OBJ) $(USER_CRT_OBJS)

$(USER_COUNT_ELF): $(USER_COUNT_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_COUNT_OBJ)

$(USER_FORK_OBJ): CodeE-User/Apps/Fork.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_WAITNH_OBJ): CodeE-User/Apps/WaitNoHang.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_LIBTOY_OBJ): CodeE-User/Apps/LibToy.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -fPIC -c $< -o $@

$(USER_DYNDEMO_OBJ): CodeE-User/Apps/DynDemo.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_CAT_OBJ): CodeE-User/Apps/Cat.c CodeE-User/include/unistd.h CodeE-User/include/fcntl.h CodeE-User/include/errno.h CodeE-User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/Cat.c -o $@

$(USER_WRITE_OBJ): CodeE-User/Apps/WriteFile.c CodeE-User/include/unistd.h CodeE-User/include/fcntl.h CodeE-User/include/errno.h CodeE-User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/WriteFile.c -o $@

$(USER_NETDEMO_OBJ): CodeE-User/Apps/NetDemo.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_NETSRV_OBJ): CodeE-User/Apps/NetServer.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_SYSHELLO_OBJ): CodeE-User/Apps/SysHello.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_SYSFORK_OBJ): CodeE-User/Apps/SysFork.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

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

$(USER_HELLO_OBJ): CodeE-User/Apps/Hello.c CodeE-User/include/stdio.h CodeE-User/include/stdlib.h CodeE-User/include/string.h CodeE-User/include/stddef.h | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/Apps/Hello.c -o $@

$(USER_VIRT_DIR)/crt0.o: $(USER_CRT0_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_CRT0_SRC) -o $@

$(USER_VIRT_DIR)/syscall.o: $(USER_SYSCALL_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_SYSCALL_SRC) -o $@

$(USER_VIRT_DIR)/string.o: CodeE-User/crt/string.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/string.c -o $@

$(USER_VIRT_DIR)/printf.o: CodeE-User/crt/printf.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/printf.c -o $@

$(USER_VIRT_DIR)/malloc.o: CodeE-User/crt/malloc.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/malloc.c -o $@

$(USER_VIRT_DIR)/errno.o: CodeE-User/crt/errno.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/errno.c -o $@

$(USER_VIRT_DIR)/unistd.o: CodeE-User/crt/unistd.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/unistd.c -o $@

$(USER_VIRT_DIR)/sleep.o: CodeE-User/crt/sleep.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/sleep.c -o $@

$(USER_VIRT_DIR)/stdlib.o: CodeE-User/crt/stdlib.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/stdlib.c -o $@

$(USER_VIRT_DIR)/signal.o: CodeE-User/crt/signal.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/signal.c -o $@

$(USER_VIRT_DIR)/dirent.o: CodeE-User/crt/dirent.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/dirent.c -o $@

$(USER_VIRT_DIR)/stdio.o: CodeE-User/crt/stdio.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/stdio.c -o $@

$(USER_VIRT_DIR)/socket.o: CodeE-User/crt/socket.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/socket.c -o $@

$(USER_VIRT_DIR)/cwd.o: CodeE-User/crt/cwd.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/cwd.c -o $@

$(USER_VIRT_DIR)/sched.o: CodeE-User/crt/sched.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/sched.c -o $@

$(USER_VIRT_DIR)/proc.o: CodeE-User/crt/proc.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/proc.c -o $@

$(USER_VIRT_DIR)/stat.o: CodeE-User/crt/stat.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/stat.c -o $@

$(USER_VIRT_DIR)/thread_root.o: CodeE-User/crt/thread_root.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/thread_root.c -o $@

$(USER_VIRT_DIR)/pthread.o: CodeE-User/crt/pthread.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c CodeE-User/crt/pthread.c -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_VIRT_DIR)
	# 与 Kernel.elf 同：arm64 __sync_* 可能仍需 libgcc（与 -mno-outline-atomics 双保险）
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(LIBGCC)
endif
endif

clean:
	# 只清当前 Arch 的 HAL 产物；共享 Code* 产物 .o 必须清（随 ARCH 重编）
	rm -rf $(HALDIR)
	rm -rf $(BUILDDIR)/CodeA-HAL $(BUILDDIR)/CodeB-Library $(BUILDDIR)/CodeC-Core $(BUILDDIR)/CodeC-Modules $(BUILDDIR)/CodeD-Services $(BUILDDIR)/CodeE-User $(BUILDDIR)/ThirdParty/lwip $(BUILDDIR)/lwip
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
	rm -f $(USER_WINDEMO_OBJ) $(USER_GUIDEMO_OBJ) $(USER_BLITDEMO_OBJ) $(USER_LIBCDEMO_OBJ) $(USER_SLEEPDEMO_OBJ) $(USER_THREADSMOKE_OBJ) $(USER_PTHREADSMOKE_OBJ) $(USER_THREADDEMO_OBJ) $(USER_SNAKE_OBJ) $(USER_DIRDEMO_OBJ) $(USER_CWDDEMO_OBJ)
	rm -f $(USER_NETLIB_OBJ) $(USER_SOCKDEMO_OBJ) $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) $(USER_ENOSYS_OBJ)
	rm -f $(USER_LIB_TOY_GFX_OBJ) $(USER_LIB_TOY_UI_OBJ) $(USER_LIB_TOY_UI_WIDGETS_OBJ) \
		$(USER_LIB_TOY_NET_OBJ) $(USER_LIB_FSUTIL_OBJ)
	rm -f $(USER_LIB_TOY_GFX_A) $(USER_LIB_TOY_UI_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A) $(USER_LIB_TOYOS_A)
	rm -f $(USER_CRT_OBJS)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF)
	rm -f $(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
	rm -f $(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF)
	rm -f $(USER_EXECDEMO_ELF) $(USER_PIPEDEMO_ELF) $(USER_BRKDEMO_ELF) $(USER_MMAPDEMO_ELF) $(USER_KILLDEMO_ELF) $(USER_SIGDEMO_ELF)
	rm -f $(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_SLEEPDEMO_ELF) $(USER_THREADSMOKE_ELF) $(USER_PTHREADSMOKE_ELF) $(USER_THREADDEMO_ELF) $(USER_SNAKE_ELF) $(USER_DIRDEMO_ELF) $(USER_CWDDEMO_ELF)
	rm -f $(USER_NETLIB_ELF) $(USER_SOCKDEMO_ELF) $(USER_CHAT_ELF) $(USER_ENOSYS_ELF)
endif
