# libToyNet 客户端模板（PR-L4）
# 需内核 ./build.sh LWIP=1；QEMU 里 lwip on 后 exec NETLIB.ELF / MYNET.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYNET
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyNet/libToyNet.a

include $(TOYKERNEL)/User/pkg/ToyUser.mk
