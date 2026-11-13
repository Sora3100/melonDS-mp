<p align="center"><img src="https://raw.githubusercontent.com/melonDS-emu/melonDS/master/res/icon/melon_128x128.png"></p>
<h2 align="center"><b>melonDS</b></h2>
<p align="center">
<a href="http://melonds.kuribo64.net/" alt="melonDS website"><img src="https://img.shields.io/badge/website-melonds.kuribo64.net-%2331352e.svg"></a>
<a href="http://melonds.kuribo64.net/downloads.php" alt="Release: 1.1"><img src="https://img.shields.io/badge/release-1.1-%235c913b.svg"></a>
<a href="https://www.gnu.org/licenses/gpl-3.0" alt="License: GPLv3"><img src="https://img.shields.io/badge/License-GPL%20v3-%23ff554d.svg"></a>
<a href="https://kiwiirc.com/client/irc.badnik.net/?nick=IRC-Source_?#melonds" alt="IRC channel: #melonds"><img src="https://img.shields.io/badge/IRC%20chat-%23melonds-%23dd2e44.svg"></a>
<a href="https://discord.gg/pAMAtExcqV" alt="Discord"><img src="https://img.shields.io/badge/Discord-Kuribo64-7289da?logo=discord&logoColor=white"></a>
<br>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-windows.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-windows.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-ubuntu.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-ubuntu.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-macos.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-macos.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-bsd.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-bsd.yml/badge.svg" /></a>
</p>
DS emulator, sorta

The goal is to do things right and fast, akin to blargSNES (but hopefully better). But also to, you know, have a fun challenge :)
<hr>

## melonDS-mp: extended buttons and up to 8 controllers

This fork adds modern-controller inputs and multi-controller support that homebrew (or patched
games) can read from the emulated DS.

### Features

* **24 inputs per controller**: the 12 DS keys plus **L2, R2, L3, R3** and the four directions of
  the **left and right analog sticks**, with real analog values for sticks and triggers.
* **Up to 8 controllers**, each with its own keyboard mappings, joystick mappings and joystick.
* **Reworked input dialog** (*Config → Input and hotkeys → DS keypad*):
  * tabs on the left for **Controller 1-8**;
  * L2/R2 above L/R, the left stick (L3 in the middle) under the d-pad, the right stick
    (R3 in the middle) under the face buttons;
  * a joystick selector per controller with an **Auto-fill** button that fills the joystick
    mappings from SDL's game controller database, and an **Unbind all** button that sets every
    mapping on the page shown (keyboard or joystick) to *None*.
* **Hotplug**: a newly connected joystick goes to a free controller: one reserved for its model
  (saved in the config, or last unplugged from it), else the one matching its player slot
  (XInput LED 3 → Controller 3), else the first free one. A controller that is reserved for a
  different model, or set to *(none)* in the dialog, is never filled automatically. The assignment
  is saved, so each joystick keeps its controller across restarts. If that controller's joystick
  mappings are all *None*, they are filled in automatically (Nintendo layout by position:
  A = right, B = bottom, X = top, Y = left).
* Controllers are remembered by model (bus, vendor and product from the SDL GUID in `JoystickGUID`)
  plus device number, so identical pads stay apart; `JoystickGUID = "none"` keeps a controller
  empty. A joystick is never given to two controllers: choosing it for one controller removes it
  from the other.
* **Toggles** at the top of the DS keypad tab: *Extended buttons* and *Multiple controllers*
  (both on by default). With both off, melonDS behaves exactly like upstream.
* **CPU overclock** (*Config → Emu settings → CPU emulation*): a slider from 100% (stock 67 MHz
  ARM9) to 1000% in 10% steps. It speeds up the ARM9 only (interpreter and JIT), takes effect
  immediately without a reset, and can remove slowdown in games that lag on real hardware. Games
  that depend on exact timing may misbehave above 100%.

### Where the controllers live

**Controller 1** is the DS itself. Its extra inputs also appear in the reserved bits of the DS key
registers. They are **active-high** (1 = pressed), so they read 0 when nothing is pressed, exactly
like the reserved bits on hardware. The normal DS keys keep their usual active-low meaning.

| Register | Bits |
|---|---|
| `KEYINPUT` `0x04000130` (ARM9 + ARM7) | 10 L2, 11 R2, 12 L3, 13 R3 |
| `EXTKEYIN` `0x04000136` (ARM7 only) | 8 LS Right, 9 LS Left, 10 LS Up, 11 LS Down, 12 RS Right, 13 RS Left, 14 RS Up, 15 RS Down |

**All 8 controllers** are in a read-only block at **`0x04FFF000`**, readable from both CPUs with
8-, 16- or 32-bit reads. Check the magic first: on real hardware or upstream melonDS this
range reads 0.

| Offset | Type | Meaning |
|---|---|---|
| `0x00` | u32 | magic `'MPAD'` = `0x4441504D` |
| `0x04` | u16 | version (1) |
| `0x06` | u8 | controller count (8) |
| `0x07` | u8 | connected mask (bit *n* = controller *n*+1 present) |
| `0x08` | u32 | update counter (+1 every emulated frame) |
| `0x0C` | u32 | allocated cheat memory size (0 = none), see below |
| `0x10 + n*0x10` | 16 bytes | controller *n*+1 (*n* = 0..7) |

Each controller entry:

| Offset | Type | Meaning |
|---|---|---|
| `+0x0` | u32 | button word, **all active-high** (below) |
| `+0x4` | s8 ×4 | left X, left Y, right X, right Y (-128..127, +Y = down) |
| `+0x8` | u8 ×2 | L2, R2 analog (0..255) |
| `+0xA` | — | reserved (0) |

Button word (KEYINPUT layout in the low half, EXTKEYIN layout in the high half):

| Bit | Input | Bit | Input |
|---|---|---|---|
| 0 | A | 10 | L2 |
| 1 | B | 11 | R2 |
| 2 | Select | 12 | L3 |
| 3 | Start | 13 | R3 |
| 4 | Right | 16 | X |
| 5 | Left | 17 | Y |
| 6 | Up | 24-27 | left stick Right / Left / Up / Down |
| 7 | Down | 28-31 | right stick Right / Left / Up / Down |
| 8 | R | | |
| 9 | L | | |

What each toggle exposes:

| Extended buttons | Multiple controllers | Result |
|---|---|---|
| off | off | upstream behaviour: no extra register bits, nothing at `0x04FFF000` |
| on | off | extra register bits; block with controller 1 only |
| off | on | block with controllers 1-8, DS keys only (extra bits, axes and triggers read 0) |
| on | on | everything |

Reading it from C:

```c
#define MPAD_BASE 0x04FFF000
#define MPAD_REG32(off) (*(volatile unsigned int*)(MPAD_BASE + (off)))

int mpad_present(void)        { return MPAD_REG32(0x00) == 0x4441504D; }
int mpad_connected(int n)     { return (MPAD_REG32(0x04) >> 24) >> n & 1; }
unsigned mpad_buttons(int n)  { return MPAD_REG32(0x10 + n * 0x10); }
signed char mpad_axis(int n, int a) /* a: 0 LX, 1 LY, 2 RX, 3 RY */
{
    return (signed char)(MPAD_REG32(0x14 + n * 0x10) >> (8 * a));
}
unsigned char mpad_trigger(int n, int t) /* t: 0 L2, 1 R2 */
{
    return (unsigned char)(MPAD_REG32(0x18 + n * 0x10) >> (8 * t));
}
```

The layout is also documented in [`src/NDS.h`](./src/NDS.h) (`MultiPadBase`).

### Configuration

Per instance in `melonDS.toml`:

* controller 1: `[InstanceN.Keyboard]`, `[InstanceN.Joystick]`, `JoystickID` and `JoystickGUID` in `[InstanceN]`;
* controllers 2-8: `[InstanceN.PadK.Keyboard]`, `[InstanceN.PadK.Joystick]`, `JoystickID` and `JoystickGUID` in
  `[InstanceN.PadK]` (*K* = 2..8, -1 = no joystick);
* new mapping keys: `L2 R2 L3 R3 LStickRight LStickLeft LStickUp LStickDown RStickRight
  RStickLeft RStickUp RStickDown`;
* toggles: `ExtendedButtons` and `MultiController` in `[InstanceN]`.
* CPU overclock: `CPUOverclock` in `[Emu]` (percent, 100-1000, default 100).

### Cheat memory (code caves)

An Action Replay code type allocates extra RAM that the game can use, e.g. for patch code and data:

```
CF000000 SSSSSSSS
```

* Allocates `SSSSSSSS` bytes of zero-filled RAM at **`0x0E000000`** (rounded up to a power of two,
  4 KB minimum, **4 MB** maximum) and sets the code's offset register to `0x0E000000`, so the
  following lines of the same code can fill it relative to that address.
* `0x0E000000` is unused on both DS and DSi and inside the range ordinary AR codes can write
  (`0x00000000-0x0FFFFFFF`).
* The memory is mapped for **ARM9 and ARM7**: CPU reads/writes, DMA and code execution. On the ARM9
  it is always **readable, writable and executable** no matter how the game set up the MPU (no
  data/prefetch aborts) and is never cached.
* Works with the **interpreter and the JIT**: code there is compiled like any other code, and
  writes that change a compiled instruction invalidate it.
* Running the code again (cheats run every frame) never shrinks or clears it; a larger size grows
  it and keeps the contents. It is freed on reset and stored in savestates (states without it
  still load).
* The allocated size is also reported at `0x04FFF00C` in the multi-pad block (0 = none).

Example: allocate 64 KB, copy a function (`mov r0,#0x42 ; bx lr`) and two data words to the start:

```
CF000000 00010000
E0000000 00000010
E3A00042 E12FFF1E
12345678 CAFEBABE
```

A game patch can then branch into it, e.g. with a `bl`/`blx` written over an existing instruction
by a normal `0XXXXXXX` code.

### Standalone Windows build (no DLLs)

Static build with MSYS2 UCRT64, Qt 5 static and a static SDL2 built from source (MSYS2's SDL2 is
sdl2-compat, which always loads SDL3.dll):

```
pacman -S mingw-w64-ucrt-x86_64-qt5-static
git clone --depth 1 --branch release-2.32.10 https://github.com/libsdl-org/SDL.git SDL2-src
cmake -S SDL2-src -B SDL2-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX=$PWD/sdl2-static
cmake --build SDL2-build && cmake --install SDL2-build
PKG_CONFIG_PATH=$PWD/sdl2-static/lib/pkgconfig:$PKG_CONFIG_PATH cmake -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -DUSE_QT6=OFF -DCMAKE_PREFIX_PATH="$MSYSTEM_PREFIX/qt5-static;$PWD/sdl2-static"
cmake --build build-static
```

`build-static/melonDS.exe` then only needs Windows' own DLLs. For the DLL build, ship a real SDL2.dll (same source,
`-DSDL_SHARED=ON`) rather than MSYS2's sdl2-compat: under SDL3 the XInput axis order is
LX, LY, LT, RX, RY, RT instead of SDL2's LX, LY, RX, RY, LT, RT, so saved axis mappings would point at the
wrong axes. MSYS2's static libarchive expects
zlib import pointers; `mingw_static_zlib.c` provides them in static MinGW builds.

### Test homebrew

[`homebrew/mpadtest`](./homebrew/mpadtest) is a small bare-metal test ROM (no libnds needed) that
shows all 8 controllers live: controllers 1-4 on the top screen, 5-8 on the bottom. Each panel shows
the buttons, L2/R2 analog bars, both sticks with L3/R3, the raw button word, and (controller 1)
the raw `KEYINPUT`. Build it with `homebrew/mpadtest/build.sh` (needs `arm-none-eabi-gcc` and
Python 3) to get `mpadtest.nds`.

[`homebrew/cavetest`](./homebrew/cavetest) tests the cheat memory: put `cavetest.mch` next to
`cavetest.nds`, enable cheats, and the ROM checks that it can read, call, write and rewrite code at
`0x0E000000` with the MPU on and that region left out of it.

<hr>

## How to use

Firmware boot (not direct boot) requires a BIOS/firmware dump from an original DS or DS Lite.
DS firmwares dumped from a DSi or 3DS aren't bootable and only contain configuration data, thus they are only suitable when booting games directly.

### Possible firmware sizes

 * 128KB: DSi/3DS DS-mode firmware (reduced size due to lacking bootcode)
 * 256KB: regular DS firmware
 * 512KB: iQue DS firmware

DS BIOS dumps from a DSi or 3DS can be used with no compatibility issues. DSi BIOS dumps (in DSi mode) are not compatible. Or maybe they are. I don't know.

As for the rest, the interface should be pretty straightforward. If you have a question, don't hesitate to ask, though!

## How to build
See [BUILD.md](./BUILD.md) for build instructions.

## TODO LIST

 * better DSi emulation
 * better OpenGL rendering
 * netplay
 * the impossible quest of pixel-perfect 3D graphics
 * support for rendering screens to separate windows
 * emulating some fancy addons
 * other non-core shit (debugger, graphics viewers, etc)

### TODO LIST FOR LATER (low priority)

 * big-endian compatibility (Wii, etc)
 * LCD refresh time (used by some games for blending effects)
 * any feature you can eventually ask for that isn't outright stupid

## Credits

 * Martin for GBAtek, a good piece of documentation
 * Cydrak for the extra 3D GPU research
 * limittox for the icon
 * All of you comrades who have been testing melonDS, reporting issues, suggesting shit, etc

## Licenses

[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

melonDS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

### External
* Images used in the Input Config Dialog - see `src/frontend/qt_sdl/InputConfig/resources/LICENSE.md`
