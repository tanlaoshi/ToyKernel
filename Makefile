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
                  -IFonts \
                  -IHAL/$(HAL_ARCH)
INCLUDES_HAL    = $(INCLUDES_COMMON) \
                  -IHAL/$(HAL_ARCH)/Drivers

CFLAGS_BASE = -ffreestanding -nostdlib -O2 -Wall -Wextra \
              -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
              -DTOY_DEBUG=$(DEBUG) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
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
	$(LWIPDIR)/core/ipv4/etharp.c \
	$(LWIPDIR)/core/ipv4/icmp.c \
	$(LWIPDIR)/core/ipv4/ip4.c \
	$(LWIPDIR)/core/ipv4/ip4_addr.c \
	$(LWIPDIR)/netif/ethernet.c
LWIPOBJS = $(patsubst $(LWIPDIR)/%.c,$(BUILDDIR)/lwip/%.o,$(LWIPCORE))
LWIP_PORT_SRCS = HAL/$(HAL_ARCH)/LwIp/toy_netif.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_ping.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_tcpecho.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_udp.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_tcpclient.c \
                 HAL/$(HAL_ARCH)/LwIp/toy_socket.c
LWIP_PORT_OBJS = $(patsubst HAL/$(HAL_ARCH)/LwIp/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/LwIp/%.o,$(LWIP_PORT_SRCS))
endif

CFLAGS_COMMON = $(CFLAGS_BASE) $(INCLUDES_COMMON) $(LWIPINCLUDES)
CFLAGS_HAL    = $(CFLAGS_BASE) $(INCLUDES_HAL) $(LWIPINCLUDES) -IHAL/$(HAL_ARCH)/LwIp

LDFLAGS = -nostdlib -static -T HAL/$(HAL_ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)

BUILDDIR = Build

CORE_SRCS     := $(wildcard Common/Core/*.c)
SERVICES_SRCS := $(wildcard Common/Services/*.c)
LIB_SRCS      := $(wildcard Common/Library/*.c)
FONT_SRCS     := $(wildcard Fonts/*.c)
DRIVER_SRCS   := $(wildcard HAL/$(HAL_ARCH)/Drivers/*.c)
ARCH_SRCS     := $(wildcard HAL/$(HAL_ARCH)/*.c)
ARCH_ASM_ALL  := $(wildcard HAL/$(HAL_ARCH)/*.S)
ARCH_ASM      := $(filter-out HAL/$(HAL_ARCH)/SmpTrampoline.S,$(ARCH_ASM_ALL))

CORE_OBJS     := $(patsubst Common/Core/%.c,$(BUILDDIR)/Common/Core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst Common/Services/%.c,$(BUILDDIR)/Common/Services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst Common/Library/%.c,$(BUILDDIR)/Common/Library/%.o,$(LIB_SRCS))
FONT_OBJS     := $(patsubst Fonts/%.c,$(BUILDDIR)/Fonts/%.o,$(FONT_SRCS))
DRIVER_OBJS   := $(patsubst HAL/$(HAL_ARCH)/Drivers/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/Drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst HAL/$(HAL_ARCH)/%.c,$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst HAL/$(HAL_ARCH)/%.S,$(BUILDDIR)/HAL/$(HAL_ARCH)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(BUILDDIR)/User_hello_blob.o $(BUILDDIR)/SmpTramp_blob.o
USER_HELLO_ELF = User/hello.elf
USER_COUNT_ELF = User/count.elf
USER_FORK_ELF = User/fork.elf
USER_WAITNH_ELF = User/waitnh.elf
USER_DYNDEMO_ELF = User/dyndemo.elf
USER_LIBTOY_SO = User/libtoy.so
USER_CAT_ELF = User/catfile.elf
USER_WRITE_ELF = User/writefile.elf
USER_NETDEMO_ELF = User/netdemo.elf
USER_NETSRV_ELF = User/netsrv.elf
USER_SYSHELLO_ELF = User/syshello.elf
USER_SYSFORK_ELF = User/sysfork.elf
USER_HELLO_OBJ = User/hello.o
USER_COUNT_OBJ = User/count.o
USER_FORK_OBJ = User/fork.o
USER_WAITNH_OBJ = User/waitnh.o
USER_DYNDEMO_OBJ = User/dyndemo.o
USER_LIBTOY_OBJ = User/libtoy.o
USER_CAT_OBJ = User/catfile.o
USER_WRITE_OBJ = User/writefile.o
USER_NETDEMO_OBJ = User/netdemo.o
USER_NETSRV_OBJ = User/netsrv.o
USER_SYSHELLO_OBJ = User/syshello.o
USER_SYSFORK_OBJ = User/sysfork.o
USER_LD = User/user.ld
endif

OBJS = $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(FONT_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS) $(LWIPOBJS) $(LWIP_PORT_OBJS)
TARGET = $(BUILDDIR)/Kernel.elf

.PHONY: all clean

all: $(TARGET)
ifeq ($(ARCH),x86_64)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF) \
	$(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF) \
	$(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF)
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

$(BUILDDIR)/Fonts/%.o: Fonts/%.c | $(BUILDDIR)
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
$(BUILDDIR)/SmpTramp.bin: HAL/X86_64/SmpTrampoline.S HAL/X86_64/SmpTrampoline.ld | $(BUILDDIR)
	$(CC) -c HAL/X86_64/SmpTrampoline.S -o $(BUILDDIR)/SmpTrampoline_low.o
	$(LD) -T HAL/X86_64/SmpTrampoline.ld -o $(BUILDDIR)/SmpTrampoline_low.elf \
		$(BUILDDIR)/SmpTrampoline_low.o
	objcopy -O binary $(BUILDDIR)/SmpTrampoline_low.elf $@

$(BUILDDIR)/SmpTramp_blob.o: $(BUILDDIR)/SmpTramp.bin
	cd $(BUILDDIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		SmpTramp.bin SmpTramp_blob.o

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

$(USER_WAITNH_OBJ): User/waitnh.S
	$(CC) -c $< -o $@

$(USER_LIBTOY_OBJ): User/libtoy.S
	$(CC) -fPIC -c $< -o $@

$(USER_DYNDEMO_OBJ): User/dyndemo.S
	$(CC) -c $< -o $@

$(USER_CAT_OBJ): User/catfile.S
	$(CC) -c $< -o $@

$(USER_WRITE_OBJ): User/writefile.S
	$(CC) -c $< -o $@

$(USER_NETDEMO_OBJ): User/netdemo.S
	$(CC) -c $< -o $@

$(USER_NETSRV_OBJ): User/netsrv.S
	$(CC) -c $< -o $@

$(USER_SYSHELLO_OBJ): User/syshello.S
	$(CC) -c $< -o $@

$(USER_SYSFORK_OBJ): User/sysfork.S
	$(CC) -c $< -o $@

$(USER_FORK_ELF): $(USER_FORK_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_FORK_OBJ)

$(USER_WAITNH_ELF): $(USER_WAITNH_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_WAITNH_OBJ)

$(USER_LIBTOY_SO): $(USER_LIBTOY_OBJ)
	$(LD) -shared -soname LIBTOY.SO -o $@ $(USER_LIBTOY_OBJ)

$(USER_DYNDEMO_ELF): $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO)
	$(LD) -nostdlib -no-pie -Ttext-segment=0x40000000 -z max-page-size=0x1000 \
		-o $@ $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO)

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_CAT_OBJ)

$(USER_WRITE_ELF): $(USER_WRITE_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_WRITE_OBJ)

$(USER_NETDEMO_ELF): $(USER_NETDEMO_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_NETDEMO_OBJ)

$(USER_NETSRV_ELF): $(USER_NETSRV_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_NETSRV_OBJ)

$(USER_SYSHELLO_ELF): $(USER_SYSHELLO_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_SYSHELLO_OBJ)

$(USER_SYSFORK_ELF): $(USER_SYSFORK_OBJ) $(USER_LD)
	$(LD) -nostdlib -static -T $(USER_LD) -o $@ $(USER_SYSFORK_OBJ)

$(BUILDDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(BUILDDIR)
	objcopy -I binary -O $(USER_BLOB_FMT) User/hello.elf $@
endif

clean:
	rm -rf $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_FORK_OBJ) $(USER_WAITNH_OBJ)
	rm -f $(USER_LIBTOY_OBJ) $(USER_DYNDEMO_OBJ) $(USER_CAT_OBJ) $(USER_WRITE_OBJ)
	rm -f $(USER_NETDEMO_OBJ) $(USER_NETSRV_OBJ) $(USER_SYSHELLO_OBJ) $(USER_SYSFORK_OBJ)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF)
	rm -f $(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
	rm -f $(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF)
endif
