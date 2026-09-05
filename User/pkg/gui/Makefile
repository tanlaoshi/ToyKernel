# GUI 程序模板（PR-L3）— 链 libToyUi + libToyGfx
#   make -C User/pkg/gui
#   cp build/MYGUI.ELF ../../ToyImage/rootfs/
#   toyos> exec MYGUI.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYGUI
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a

include $(TOYKERNEL)/User/pkg/ToyUser.mk
