# ToyOS SDK 应用规则（PR-A-sdk-pack）
# 用法：设 TOYSDK 指向 Dist/ToySdk 根，再 include 本文件。
#
# 调用方需提供：
#   PROG   — 产物基名（默认 MYAPP；盘上建议大写 8.3：MYAPP.ELF）
#   SRCS   — .c 源列表（默认 main.c）
# 可选：
#   TOYSDK     — 默认本文件所在目录（本文件位于 SDK 根时）
#   EXTRA_LIBS — 额外 .a（如 $(TOYSDK)/Library/libToyUi.a）
#   EXTRA_OBJS — 额外 .o
#   TOYIMAGE   — ToyImage 仓（deploy/run）；默认 SDK 在
#                ToyKernel/Dist/ToySdk 时的兄弟仓 ../../../ToyImage

ifndef TOYSDK
TOYSDK := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
endif

PROG      ?= MYAPP
SRCS      ?= main.c
BUILDDIR  ?= Build
ELF       := $(BUILDDIR)/$(PROG).ELF

CC        ?= gcc
LD        ?= ld

USER_INC  := $(TOYSDK)/include
USER_LD   := $(TOYSDK)/user.ld
LIB_DIR   := $(TOYSDK)/Library

USER_CFLAGS ?= -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone -I$(USER_INC)

CRT0_OBJ    := $(LIB_DIR)/crt0.o
SYSCALL_OBJ := $(LIB_DIR)/syscall.o
LIBTOYOS_A  := $(LIB_DIR)/libtoyos.a

TOYIMAGE ?= $(abspath $(TOYSDK)/../../../ToyImage)

OBJS := $(addprefix $(BUILDDIR)/,$(SRCS:.c=.o))

.PHONY: all clean deploy run

all: $(ELF)

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(CRT0_OBJ) $(SYSCALL_OBJ) $(LIBTOYOS_A) $(USER_LD) $(EXTRA_OBJS) $(EXTRA_LIBS)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(OBJS) $(EXTRA_OBJS) \
		$(EXTRA_LIBS) $(LIBTOYOS_A) $(CRT0_OBJ) $(SYSCALL_OBJ)

deploy: $(ELF)
	@if [ ! -d "$(TOYIMAGE)/rootfs" ]; then \
		echo "deploy: set TOYIMAGE to the ToyImage repo (need rootfs/). TOYIMAGE=$(TOYIMAGE)"; \
		exit 1; \
	fi
	cp -f $(ELF) $(TOYIMAGE)/rootfs/$(PROG).ELF
	@echo "copied $(PROG).ELF -> $(TOYIMAGE)/rootfs/"

run: deploy
	cd $(TOYIMAGE) && ./run-split.sh

clean:
	rm -rf $(BUILDDIR)
