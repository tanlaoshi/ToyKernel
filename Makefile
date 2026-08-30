ARCH ?= x86_64

CC = gcc
LD = ld

INCLUDES = -Icommon -Ihal -Ihal/$(ARCH)

CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra \
         -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
         $(INCLUDES)

ifeq ($(ARCH),x86_64)
CFLAGS += -m64 -mno-red-zone
LDFLAGS_ARCH =
USER_BLOB_FMT = elf64-x86-64
endif
ifeq ($(ARCH),riscv)
CFLAGS += -march=rv64gc -mabi=lp64d
LDFLAGS_ARCH =
endif
ifeq ($(ARCH),arm64)
CFLAGS += -mgeneral-regs-only
LDFLAGS_ARCH =
endif

LDFLAGS = -nostdlib -static -T hal/$(ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)

BUILDDIR = Build
COMMON_SRCS := $(wildcard common/*.c)
ARCH_SRCS := $(wildcard hal/$(ARCH)/*.c)
ARCH_ASM := $(wildcard hal/$(ARCH)/*.S)

COMMON_OBJS := $(patsubst common/%.c,$(BUILDDIR)/common/%.o,$(COMMON_SRCS))
ARCH_OBJS := $(patsubst hal/$(ARCH)/%.c,$(BUILDDIR)/hal/$(ARCH)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst hal/$(ARCH)/%.S,$(BUILDDIR)/hal/$(ARCH)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(BUILDDIR)/user_hello_blob.o
USER_HELLO_ELF = user/hello.elf
USER_COUNT_ELF = user/count.elf
USER_HELLO_OBJ = user/hello.o
USER_COUNT_OBJ = user/count.o
USER_LD = user/user.ld
endif

OBJS = $(COMMON_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS)
TARGET = $(BUILDDIR)/Kernel.elf

.PHONY: all clean

all: $(TARGET)
ifeq ($(ARCH),x86_64)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF)
endif

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/common/%.o: common/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/hal/$(ARCH)/%.o: hal/$(ARCH)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/hal/$(ARCH)/%.o: hal/$(ARCH)/%.S | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(USER_HELLO_OBJ): user/hello.S
	$(CC) -c $< -o $@

$(USER_COUNT_OBJ): user/count.S
	$(CC) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_HELLO_OBJ)

$(USER_COUNT_ELF): $(USER_COUNT_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_COUNT_OBJ)

$(BUILDDIR)/user_hello_blob.o: $(USER_HELLO_ELF) | $(BUILDDIR)
	objcopy -I binary -O $(USER_BLOB_FMT) user/hello.elf $@
endif

clean:
	rm -rf $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_HELLO_ELF) $(USER_COUNT_ELF)
endif
