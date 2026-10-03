# Style — ToyUiStyle 范例（PR-UI-style-0）
#   make -C CodeE-User/Pkg/Style
#   exec STYLE.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= STYLE
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/CodeE-User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/CodeE-User/Library/ToyGfx/libToyGfx.a

include ../ToyUser.mk
