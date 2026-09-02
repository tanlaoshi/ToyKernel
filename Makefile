ARCH ?= x86_64
DEBUG ?= 0
LWIP ?= 0
LWIPINCLUDES :=
LWIPOBJS :=
LWIP_PORT_OBJS :=

CC = gcc
LD = ld

ifeq ($(ARCH),x86_64)
HAL_ARCH = X86_64
ARCH_CFLAGS = -m64 -mno-red-zone
USER_BLOB_FMT = elf64-x86-64
LDFLAGS_ARCH =
endif
ifeq ($(ARCH),riscv)
HAL_ARCH = RiscV
ARCH_CFLAGS = -march=rv64gc -mabi=lp64d
LDFLAGS_ARCH =
endif
ifeq ($(ARCH),arm64)
HAL_ARCH = Arm64
ARCH_CFLAGS = -mgeneral-regs-only
LDFLAGS_ARCH =
endif

INCLUDES_COMMON = -IInclude \
                  -ICommon/Library \
                  -IHAL/$(HAL_ARCH)
INCLUDES_HAL    = $(INCLUDES_COMMON) \
                  -IHAL/$(HAL_ARCH)/Drivers

CFLAGS_BASE = -ffreestanding -nostdlib -O2 -Wall -Wextra \
              -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
              -DTOY_DEBUG=$(DEBUG) \
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
	$(LWIPDIR)/core/stats.c \
	$(LWIPDIR)/core/sys.c \
	$(LWIPDIR)/core/tcp.c \
	$(LWIPDIR)/core/tcp_in.c \
	$(LWIPDIR)/core/tcp_out.c \
	$(LWIPDIR)/core/timeouts.c \
	$(LWIPDIR)/core/udp.c \
	$(LWIPDIR)/core/ipv4/etharp.c \
	$(LWIPDIR)/core/ipv4/icmp.c \
	$(LWIPDIR)/core/ipv4/ip4.c \
	$(LWIPDIR)/core/ipv4/ip4_addr.c \
	$(LWIPDIR)/netif/ethernet.c
LWIPOBJS = $(patsubst $(LWIPDIR)/%.c,$(BUILDDIR)/lwip/%.o,$(LWIPCORE))
LWIP_PORT_SRCS = HAL/$(HAL_ARCH)/LwIp/toy_netif.c
LWIP_PORT_OBJS = $(patsubst HAL/$(HAL_ARCH)/LwIp/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/LwIp/%.o,$(LWIP_PORT_SRCS))
endif

CFLAGS_COMMON = $(CFLAGS_BASE) $(INCLUDES_COMMON) $(LWIPINCLUDES)
CFLAGS_HAL    = $(CFLAGS_BASE) $(INCLUDES_HAL) $(LWIPINCLUDES) -IHAL/$(HAL_ARCH)/LwIp

LDFLAGS = -nostdlib -static -T HAL/$(HAL_ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)

BUILDDIR = Build

CORE_SRCS     := $(wildcard Common/Core/*.c)
SERVICES_SRCS := $(wildcard Common/Services/*.c)
LIB_SRCS      := $(wildcard Common/Library/*.c)
DRIVER_SRCS   := $(wildcard HAL/$(HAL_ARCH)/Drivers/*.c)
ARCH_SRCS     := $(wildcard HAL/$(HAL_ARCH)/*.c)
ARCH_ASM      := $(wildcard HAL/$(HAL_ARCH)/*.S)

CORE_OBJS     := $(patsubst Common/Core/%.c,$(BUILDDIR)/Common/Core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst Common/Services/%.c,$(BUILDDIR)/Common/Services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst Common/Library/%.c,$(BUILDDIR)/Common/Library/%.o,$(LIB_SRCS))
DRIVER_OBJS   := $(patsubst HAL/$(HAL_ARCH)/Drivers/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/Drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst HAL/$(HAL_ARCH)/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst HAL/$(HAL_ARCH)/%.S,$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(BUILDDIR)/User_hello_blob.o
USER_HELLO_ELF = User/hello.elf
USER_COUNT_ELF = User/count.elf
USER_FORK_ELF = User/fork.elf
USER_CAT_ELF = User/catfile.elf
USER_WRITE_ELF = User/writefile.elf
USER_HELLO_OBJ = User/hello.o
USER_COUNT_OBJ = User/count.o
USER_FORK_OBJ = User/fork.o
USER_CAT_OBJ = User/catfile.o
USER_WRITE_OBJ = User/writefile.o
USER_LD = User/user.ld
endif

OBJS = $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS) $(LWIPOBJS) $(LWIP_PORT_OBJS)
TARGET = $(BUILDDIR)/Kernel.elf

.PHONY: all clean

all: $(TARGET)
ifeq ($(ARCH),x86_64)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
endif

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/Common/Core/%.o: Common/Core/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Common/Services/%.o: Common/Services/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Common/Library/%.o: Common/Library/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/HAL/$(HAL_ARCH)/Drivers/%.o: HAL/$(HAL_ARCH)/Drivers/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o: HAL/$(HAL_ARCH)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

$(BUILDDIR)/HAL/$(HAL_ARCH)/LwIp/%.o: HAL/$(HAL_ARCH)/LwIp/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(LWIP),1)
$(BUILDDIR)/lwip/%.o: $(LWIPDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o: HAL/$(HAL_ARCH)/%.S | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_HAL) -c $< -o $@

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

$(USER_WRITE_OBJ): User/writefile.S
	$(CC) -c $< -o $@

$(USER_FORK_ELF): $(USER_FORK_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_FORK_OBJ)

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_CAT_OBJ)

$(USER_WRITE_ELF): $(USER_WRITE_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_WRITE_OBJ)

$(BUILDDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(BUILDDIR)
	objcopy -I binary -O $(USER_BLOB_FMT) User/hello.elf $@
endif

clean:
	rm -rf $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_FORK_OBJ) $(USER_CAT_OBJ) $(USER_WRITE_OBJ)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
endif
