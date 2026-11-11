#!/bin/sh
# Builds cavetest.nds with the MSYS2 arm-none-eabi toolchain (no libnds needed)
set -e
cd "$(dirname "$0")"
mkdir -p build
CFLAGS="-mcpu=arm946e-s -marm -O2 -ffreestanding -fno-builtin -Wall"
arm-none-eabi-gcc $CFLAGS -c crt0.s -o build/crt0.o
arm-none-eabi-gcc $CFLAGS -c main.c -o build/main.o
arm-none-eabi-gcc $CFLAGS -nostdlib -T arm9.ld build/crt0.o build/main.o -lgcc -o build/arm9.elf
arm-none-eabi-objcopy -O binary build/arm9.elf build/arm9.bin
arm-none-eabi-gcc -mcpu=arm7tdmi -c arm7.s -o build/arm7.o
arm-none-eabi-ld -Ttext=0x037F8000 build/arm7.o -o build/arm7.elf
arm-none-eabi-objcopy -O binary build/arm7.elf build/arm7.bin
python mknds.py build/arm9.bin build/arm7.bin cavetest.nds
