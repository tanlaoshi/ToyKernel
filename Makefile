ARCH ?= x86_64
DEBUG ?= 0

CC = gcc
LD = ld

INCLUDES = -IInclude \
           -ICommon/core -ICommon/services -ICommon/lib \
           -IHAL -IHAL/$(ARCH) -IHAL/$(ARCH)/drivers

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

LDFLAGS = -nostdlib -static -T HAL/$(ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)

BUILDDIR = Build

CORE_SRCS     := $(wildcard Common/core/*.c)
SERVICES_SRCS := $(wildcard Common/services/*.c)
LIB_SRCS      := $(wildcard Common/lib/*.c)
DRIVER_SRCS   := $(wildcard HAL/$(ARCH)/drivers/*.c)
ARCH_SRCS     := $(wildcard HAL/$(ARCH)/*.c)
ARCH_ASM      := $(wildcard HAL/$(ARCH)/*.S)

CORE_OBJS     := $(patsubst Common/core/%.c,$(BUILDDIR)/Common/core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst Common/services/%.c,$(BUILDDIR)/Common/services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst Common/lib/%.c,$(BUILDDIR)/Common/lib/%.o,$(LIB_SRCS))
DRIVER_OBJS   := $(patsubst HAL/$(ARCH)/drivers/%.c,$(BUILDDIR)/HAL/$(ARCH)/drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst HAL/$(ARCH)/%.c,$(BUILDDIR)/HAL/$(ARCH)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst HAL/$(ARCH)/%.S,$(BUILDDIR)/HAL/$(ARCH)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(BUILDDIR)/User_hello_blob.o
USER_HELLO_ELF = User/hello.elf
USER_COUNT_ELF = User/count.elf
USER_FORK_ELF = User/fork.elf
USER_CAT_ELF = User/catfile.elf
USER_HELLO_OBJ = User/hello.o
USER_COUNT_OBJ = User/count.o
USER_FORK_OBJ = User/fork.o
USER_CAT_OBJ = User/catfile.o
USER_LD = User/user.ld
endif

OBJS = $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS)
TARGET = $(BUILDDIR)/Kernel.elf

.PHONY: all clean

all: $(TARGET)
ifeq ($(ARCH),x86_64)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_CAT_ELF)
endif

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/Common/core/%.o: Common/core/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/Common/services/%.o: Common/services/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/Common/lib/%.o: Common/lib/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/HAL/$(ARCH)/drivers/%.o: HAL/$(ARCH)/drivers/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/HAL/$(ARCH)/%.o: HAL/$(ARCH)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/HAL/$(ARCH)/%.o: HAL/$(ARCH)/%.S | $(BUILDDIR)
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

$(USER_FORK_OBJ): User/fork.S
	$(CC) -c $< -o $@

$(USER_CAT_OBJ): User/catfile.S
	$(CC) -c $< -o $@

$(USER_FORK_ELF): $(USER_FORK_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_FORK_OBJ)

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_CAT_OBJ)

$(BUILDDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(BUILDDIR)
	objcopy -I binary -O $(USER_BLOB_FMT) User/hello.elf $@
endif

clean:
	rm -rf $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_FORK_OBJ) $(USER_CAT_OBJ)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_CAT_ELF)
endif
