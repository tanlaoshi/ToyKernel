CC = clang
LD = ld.lld
CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -g \
         -fno-stack-protector -fno-pie -fno-pic \
         -m64 -I.
LDFLAGS = -nostdlib -static -no-pie -e KernelEntry

TARGET = Kernel.elf
SRCS = Kernel.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean