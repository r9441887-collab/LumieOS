@echo off
chcp 65001 >nul
echo === LumieOS Build Script ===

set CC=gcc
set CFLAGS=-ffreestanding -mno-red-zone -nostdlib -nostartfiles -fno-stack-protector -fno-strict-aliasing -fno-common -mno-mmx -mno-sse -mno-sse2 -mno-80387 -mno-stack-arg-probe -Wall -Wextra -I src
set LDFLAGS=-nostdlib -nostartfiles -Wl,-e,efi_main -Wl,--subsystem,10 -Wl,--image-base,0 -Wl,--file-alignment,0x20 -Wl,--section-alignment,0x20

if not exist build mkdir build

echo Compiling...
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
%CC% %CFLAGS% -c src\rtl8168.c   -o build\rtl8168.o

echo Linking...
%CC% %LDFLAGS% build\kernel.o build\gop.o build\keyboard.o build\terminal.o build\fat.o build\shell.o build\editor.o build\util.o build\extract.o build\net.o build\tls.o build\rtl8168.o -o BOOTX64.EFI

if %ERRORLEVEL% equ 0 (
    echo === Build successful: BOOTX64.EFI ===
    echo.
    python deploy.py
    echo.
    echo To boot: copy EFI/ folder to FAT32 USB or run 'python deploy.py --iso'
) else (
    echo === Build FAILED ===
)
