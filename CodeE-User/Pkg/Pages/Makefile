# Pages — 2 页 App（PR-UI-page-0）
#   make -C CodeE-User/Pkg/Pages
#   exec PAGES.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= PAGES
SRCS      ?= main.c Route.c PageHome.c PageSettings.c PageAbout.c

EXTRA_LIBS = $(TOYKERNEL)/CodeE-User/Library/ToyUi/libToyUi.a \
             $(TOYKERNEL)/CodeE-User/Library/ToyGfx/libToyGfx.a

include ../ToyUser.mk
