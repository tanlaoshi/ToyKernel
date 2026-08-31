ARCH ?= x86_64
DEBUG ?= 0

CC = gcc
LD = ld

INCLUDES = -ICommon -IHal -IHal/$(ARCH)

CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra \
         -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
         -DTOY_DEBUG=$(DEBUG) \
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

LDFLAGS = -nostdlib -static -T Hal/$(ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)

BUILDDIR = Build
COMMON_SRCS := $(wildcard Common/*.c)
ARCH_SRCS := $(wildcard Hal/$(ARCH)/*.c)
ARCH_ASM := $(wildcard Hal/$(ARCH)/*.S)

COMMON_OBJS := $(patsubst Common/%.c,$(BUILDDIR)/Common/%.o,$(COMMON_SRCS))
ARCH_OBJS := $(patsubst Hal/$(ARCH)/%.c,$(BUILDDIR)/Hal/$(ARCH)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst Hal/$(ARCH)/%.S,$(BUILDDIR)/Hal/$(ARCH)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(BUILDDIR)/User_hello_blob.o
USER_HELLO_ELF = User/hello.elf
USER_COUNT_ELF = User/count.elf
USER_HELLO_OBJ = User/hello.o
USER_COUNT_OBJ = User/count.o
USER_LD = User/user.ld
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

$(BUILDDIR)/Common/%.o: Common/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/Hal/$(ARCH)/%.o: Hal/$(ARCH)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/Hal/$(ARCH)/%.o: Hal/$(ARCH)/%.S | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(USER_HELLO_OBJ): User/hello.S
	$(CC) -c $< -o $@

$(USER_COUNT_OBJ): User/count.S
	$(CC) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_HELLO_OBJ)

$(USER_COUNT_ELF): $(USER_COUNT_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_COUNT_OBJ)

$(BUILDDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(BUILDDIR)
	objcopy -I binary -O $(USER_BLOB_FMT) User/hello.elf $@
endif

clean:
	rm -rf $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_HELLO_ELF) $(USER_COUNT_ELF)
endif
