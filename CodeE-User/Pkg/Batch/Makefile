# Batch — ToyGfxBatch 范例（PR-GFX-batch-0）
#   make -C CodeE-User/Pkg/Batch
#   exec BATCH.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= BATCH
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/CodeE-User/Library/ToyGfx/libToyGfx.a \
             $(TOYKERNEL)/CodeE-User/Library/ToyUi/libToyUi.a

include ../ToyUser.mk
