CC = gcc
CFLAGS = -ffreestanding -mno-red-zone -nostdlib -nostartfiles \
         -fno-stack-protector -fno-strict-aliasing -fno-common \
         -mno-mmx -mno-sse -mno-sse2 -mno-80387 -mno-stack-arg-probe \
         -Wall -Wextra -I src
LDFLAGS = -nostdlib -nostartfiles -Wl,-e,efi_main -Wl,--subsystem,10 -Wl,--image-base,0 -Wl,--file-alignment,0x20 -Wl,--section-alignment,0x20

SRCDIR = src
OBJDIR = build

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
TARGET = BOOTX64.EFI

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	$(shell if not exist $(OBJDIR) mkdir $(OBJDIR))

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@
	@echo "=== Build complete: $(TARGET) ==="

clean:
	$(shell if exist $(OBJDIR) rmdir /s /q $(OBJDIR))
	$(shell if exist $(TARGET) del $(TARGET))
