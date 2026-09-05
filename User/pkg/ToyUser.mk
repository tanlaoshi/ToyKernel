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

USER_INC  := $(TOYKERNEL)/User/include
USER_LD   := $(TOYKERNEL)/User/user.ld
CRT_DIR   := $(TOYKERNEL)/User/crt
LIB_DIR   := $(TOYKERNEL)/User/Library/ToyOs

USER_CFLAGS ?= -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone -I$(USER_INC)

CRT0_OBJ    := $(CRT_DIR)/crt0.o
SYSCALL_OBJ := $(CRT_DIR)/syscall.o
LIBTOYOS_A  := $(LIB_DIR)/libtoyos.a

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

.PHONY: all clean ensure-crt

all: ensure-crt $(ELF)

ensure-crt:
	@$(MAKE) -C $(TOYKERNEL) $(CRT0_OBJ) $(SYSCALL_OBJ) $(LIBTOYOS_A)

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(CRT0_OBJ) $(SYSCALL_OBJ) $(LIBTOYOS_A) $(USER_LD) $(EXTRA_OBJS) $(EXTRA_LIBS)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(OBJS) $(EXTRA_OBJS) \
		$(EXTRA_LIBS) $(LIBTOYOS_A) $(CRT0_OBJ) $(SYSCALL_OBJ)

clean:
	rm -rf $(BUILDDIR)
