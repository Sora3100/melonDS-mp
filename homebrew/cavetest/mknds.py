"""Builds a minimal homebrew .nds image from raw ARM9/ARM7 binaries."""
import struct, sys

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc

def align(n, a=0x200):
    return (n + a - 1) & ~(a - 1)

def main(arm9_path, arm7_path, out_path, title=b"MPADTEST"):
    arm9 = open(arm9_path, "rb").read()
    arm7 = open(arm7_path, "rb").read()

    arm9_off = 0x200
    arm7_off = align(arm9_off + len(arm9))
    total = align(arm7_off + len(arm7))

    h = bytearray(0x200)
    h[0x00:0x0C] = title.ljust(12, b"\0")[:12]
    h[0x0C:0x10] = b"####"              # homebrew game code
    h[0x10:0x12] = b"00"
    struct.pack_into("<IIII", h, 0x20, arm9_off, 0x02000000, 0x02000000, len(arm9))
    struct.pack_into("<IIII", h, 0x30, arm7_off, 0x037F8000, 0x037F8000, len(arm7))
    struct.pack_into("<II", h, 0x60, 0x00586000, 0x001808F8)
    struct.pack_into("<H", h, 0x6E, 0x0D7E)
    struct.pack_into("<I", h, 0x80, total)
    struct.pack_into("<I", h, 0x84, 0x200)
    struct.pack_into("<H", h, 0x15C, 0xCF56)
    struct.pack_into("<H", h, 0x15E, crc16(h[:0x15E]))

    rom = bytearray(max(total, 0x20000))
    rom[0:0x200] = h
    rom[arm9_off:arm9_off + len(arm9)] = arm9
    rom[arm7_off:arm7_off + len(arm7)] = arm7
    open(out_path, "wb").write(rom)
    print(f"{out_path}: arm9 {len(arm9)} bytes @0x{arm9_off:X}, arm7 {len(arm7)} bytes @0x{arm7_off:X}")

if __name__ == "__main__":
    main(*sys.argv[1:4])
