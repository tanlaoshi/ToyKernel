# ToyUser.mk — 课外 / 模板共用规则（PR-L1）
# 用法：设 TOYKERNEL 指向 ToyKernel 根，再 include 本文件。
#
# 调用方需提供：
#   PROG   — 产物基名（默认 MYAPP；盘上建议大写 8.3：MYAPP.ELF）
#   SRCS   — .c 源列表（默认 main.c）
# 可选：
#   TOYKERNEL — 默认本 mk 所在目录的 ../..
#   EXTRA_LIBS — 额外 .a（如 libToyUi.a）
#   EXTRA_OBJS — 额外 .o
#   UITXT / UIH — .uitxt → 编译期头（Tools/UiLayout/uitxt2h.py）

ifndef TOYKERNEL
TOYKERNEL := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
endif

PROG      ?= MYAPP
SRCS      ?= main.c
BUILDDIR  ?= build
ELF       := $(BUILDDIR)/$(PROG).ELF

ARCH      ?= x86_64
CC        ?= gcc
LD        ?= ld
AR        ?= ar

# 树迁后源在 CodeE-User/（旧名 User/）
USER_INC  := $(TOYKERNEL)/CodeE-User/include
USER_LD   := $(TOYKERNEL)/CodeE-User/user.ld
CRT_DIR   := $(TOYKERNEL)/CodeE-User/crt
LIB_DIR   := $(TOYKERNEL)/CodeE-User/Library/ToyOs

USER_CFLAGS ?= -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone \
	-I$(USER_INC) -I$(TOYKERNEL)/Include -I.

# 链入用绝对路径；ensure-crt 调根 Makefile 须用相对目标名
CRT0_OBJ    := $(CRT_DIR)/crt0.o
SYSCALL_OBJ := $(CRT_DIR)/syscall.o
LIBTOYOS_A  := $(LIB_DIR)/libtoyos.a
CRT0_REL    := CodeE-User/crt/crt0.o
SYSCALL_REL := CodeE-User/crt/syscall.o
LIBTOYOS_REL := CodeE-User/Library/ToyOs/libtoyos.a

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

UITXT2H ?= $(TOYKERNEL)/Tools/UiLayout/uitxt2h.py

ifdef UITXT
UIH ?= $(UITXT:.uitxt=Ui.h)
endif

# EXTRA_LIBS 若给绝对路径，ensure 时剥成相对 ToyKernel 根
ENSURE_LIBS = $(patsubst $(TOYKERNEL)/%,%,$(EXTRA_LIBS))

.PHONY: all clean ensure-crt

all: ensure-crt $(ELF)

ensure-crt:
	@$(MAKE) -C $(TOYKERNEL) $(CRT0_REL) $(SYSCALL_REL) $(LIBTOYOS_REL) $(ENSURE_LIBS)

$(BUILDDIR):
	mkdir -p $@

ifdef UITXT
$(UIH): $(UITXT) $(UITXT2H)
	python3 $(UITXT2H) $< > $@

$(OBJS): $(UIH)
endif

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(CRT0_OBJ) $(SYSCALL_OBJ) $(LIBTOYOS_A) $(USER_LD) $(EXTRA_OBJS) $(EXTRA_LIBS)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(OBJS) $(EXTRA_OBJS) \
		$(EXTRA_LIBS) $(LIBTOYOS_A) $(CRT0_OBJ) $(SYSCALL_OBJ)

clean:
	rm -rf $(BUILDDIR)
ifdef UITXT
	rm -f $(UIH)
endif
