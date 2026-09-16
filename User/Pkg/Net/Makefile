# libToyNet 客户端模板（PR-L4）
# 默认内核已 LWIP=1；关栈用 ./build.sh LWIP=0。Guest：lwip on 后 exec NETLIB.ELF / MYNET.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYNET
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyNet/libToyNet.a

include $(TOYKERNEL)/User/Pkg/ToyUser.mk
