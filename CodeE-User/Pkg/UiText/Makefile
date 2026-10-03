# UiText — .uitxt → MyAppUi.h → MYAPP.ELF
#   make -C CodeE-User/Pkg/UiText

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= MYAPP
SRCS      ?= main.c UiActions.c
UITXT     ?= MyApp.uitxt
UIH       ?= MyAppUi.h
BUILDDIR  ?= build

LAYOUT_C  := $(TOYKERNEL)/Common/Services/ToyUiLayout/ToyUiLayout.c
LAYOUT_O  := $(BUILDDIR)/ToyUiLayout.o

EXTRA_LIBS = $(TOYKERNEL)/CodeE-User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/CodeE-User/Library/ToyGfx/libToyGfx.a
EXTRA_OBJS = $(LAYOUT_O)

include ../ToyUser.mk

$(LAYOUT_O): $(LAYOUT_C) $(TOYKERNEL)/Include/ToyUiLayout.h | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $(LAYOUT_C) -o $@

$(BUILDDIR)/main.o $(BUILDDIR)/UiActions.o: $(UIH)
