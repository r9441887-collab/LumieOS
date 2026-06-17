@echo off
chcp 65001 >nul
echo === LumieOS Build Script ===

set CC=gcc
set CFLAGS=-ffreestanding -mno-red-zone -nostdlib -nostartfiles -fno-stack-protector -fno-strict-aliasing -fno-common -mno-mmx -mno-sse -mno-sse2 -mno-80387 -mno-stack-arg-probe -Wall -Wextra -I src
set LDFLAGS=-nostdlib -nostartfiles -Wl,-e,efi_main -Wl,--subsystem,10 -Wl,--image-base,0 -Wl,--file-alignment,0x20 -Wl,--section-alignment,0x20

if not exist build mkdir build

:: Generate stub header so shell.c can compile
if not exist src\drivembeds.h (
    echo #ifndef __DRIVEMBEDS_H__> src\drivembeds.h
    echo #define __DRIVEMBEDS_H__>> src\drivembeds.h
    echo #include "lumie.h">> src\drivembeds.h
    echo #define DRV_EMBED_COUNT 0 >> src\drivembeds.h
    echo typedef struct { const char *name; u32 subtype; const void *data; u32 size; } drv_embed;>> src\drivembeds.h
    echo extern const drv_embed drv_embed_table[];>> src\drivembeds.h
    echo #endif>> src\drivembeds.h
)

echo Compiling...
%CC% %CFLAGS% -c src\loader.c    -o build\loader.o
%CC% %CFLAGS% -c src\kernel.c    -o build\kernel.o
%CC% %CFLAGS% -c src\gop.c       -o build\gop.o
%CC% %CFLAGS% -c src\keyboard.c  -o build\keyboard.o
%CC% %CFLAGS% -c src\terminal.c  -o build\terminal.o
%CC% %CFLAGS% -c src\fat.c       -o build\fat.o
%CC% %CFLAGS% -c src\shell.c     -o build\shell.o
%CC% %CFLAGS% -c src\editor.c    -o build\editor.o
%CC% %CFLAGS% -c src\util.c      -o build\util.o
%CC% %CFLAGS% -c src\extract.c   -o build\extract.o
%CC% %CFLAGS% -c src\net.c       -o build\net.o
%CC% %CFLAGS% -c src\tls.c       -o build\tls.o
%CC% %CFLAGS% -c src\mouse.c     -o build\mouse.o
%CC% %CFLAGS% -c src\rtl8168.c   -o build\rtl8168.o
%CC% %CFLAGS% -c src\mm.c        -o build\mm.o
%CC% %CFLAGS% -c src\ahci.c      -o build\ahci.o
%CC% %CFLAGS% -c src\ps2kbd.c    -o build\ps2kbd.o
%CC% %CFLAGS% -c src\pit.c       -o build\pit.o
%CC% %CFLAGS% -c src\ps2mouse.c  -o build\ps2mouse.o

echo Embedding drivers into kernel...
python tools\embed_o.py

echo Recompiling shell + embeds with real driver data...
%CC% %CFLAGS% -c src\shell.c     -o build\shell.o
%CC% %CFLAGS% -c src\drivembeds.c -o build\drivembeds.o

echo Linking...
%CC% %LDFLAGS% build\loader.o build\kernel.o build\gop.o build\keyboard.o build\terminal.o build\fat.o build\shell.o build\editor.o build\util.o build\extract.o build\net.o build\tls.o build\rtl8168.o build\mouse.o build\drivembeds.o build\mm.o build\ahci.o build\ps2kbd.o build\pit.o build\ps2mouse.o -o BOOTX64.EFI

if %ERRORLEVEL% equ 0 (
    echo === Build successful: BOOTX64.EFI ===
    echo.
    python deploy.py
    echo.
    echo To boot: copy EFI/ folder to FAT32 USB or run 'python deploy.py --iso'
) else (
    echo === Build FAILED ===
)
