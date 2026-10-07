# Crash Team Racing: Turbocharged on PS4

A native port of [Crash Team Racing: Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged)
(the CTR decompilation-based PC port) to jailbroken PlayStation 4 consoles. The game runs as native
x86-64 code on the console: no emulator.

| | |
|---|---|
| Tested on | PS4 Pro, firmware 12.02, GoldHEN |
| Based on | the `64bit-2026.10` branch (the experimental 64-bit build; the PS4 only runs 64-bit code) |
| Graphics | the game's PC renderer (OpenGL 3.3 core) on Mesa: EGL → zink → RADV → video out, from the [orbis-ports](https://github.com/orbis-ports) SDK |
| License | GPL-3.0, like the game it ports |

## Status

**v0.1.1:** on the console the game boots and plays, with graphics, sound, the DualShock 4 and
**memory card saves** working (tested on a PS4 Pro). Not every mode and option has been checked one
by one yet.

- v0.1.1: saves work (relative `mkdir` is now anchored to the data folder; before, the game said
  the memory card slot was full).
- v0.1.0: first release; everything but saving worked.

## You need

- A PS4 with GoldHEN (only tested on a PS4 Pro, firmware 12.02).
- **Your own NTSC-U (US) Crash Team Racing disc**, dumped as a **raw MODE2/2352 BIN** with a PC
  disc drive (ImgBurn, redumper or DiscImageCreator). A 2048-byte `.iso` does not work. No game data
  is included or provided. The PC version of Turbocharged validates a dump when you select it in its
  first-run setup; that copy (`assets/ctr-u.bin`) is the one to use here.

## Install

1. Install `ctr-turbocharged-ps4-v0.1.1.pkg` with GoldHEN (copy it by FTP in **binary** mode).
2. Start **CTR Turbocharged** once and close it: it creates `/data/ctr/` and copies its fonts there.
3. Copy your disc image to **`/data/ctr/assets/ctr-u.bin`** (FTP, binary mode).
4. Start it again.

Everything the game writes lives in `/data/ctr/`: `config.ini`, `memcards/`, logs, caches.

## Controls

The DualShock 4 works as a standard gamepad: Cross, Circle, Square and Triangle as on the PS1,
L1/R1, L2/R2, sticks and D-pad, Options = Start, touch pad click = Select. The game's own options
menu remaps them.

## Logs (for bug reports)

All in `/data/ctr/`:

- `ps4.log`: the PS4 layer: startup, GL context, controller, audio, an fps line every 10 s, and on a
  crash the registers and a backtrace as `eboot+0x...` offsets (symbolize them with the release's
  ELF: `llvm-symbolizer --obj=ctr-turbocharged-ps4-v0.1.1.elf -C -f 0x<offset>`). The previous run's
  is kept as `ps4.old.log`.
- `mesa.log`: the GPU driver.
- `Crash Team Racing- Turbocharged.log`: the game's own log.

Please report PS4-specific problems here, not to the upstream project.

## How the port works

- **No SDL on the console.** Upstream SDL3 has no PS4 support, and SDL does not accept AI-generated
  code (see below), so this port does not add a backend to it. `ps4/ps4_sdl.c` implements the 88 SDL3
  functions the game calls on the console's own APIs, compiled against SDL's public headers
  unmodified; `ps4/ps4.cmake` makes it the `SDL3::SDL3` the game links.
- **Graphics:** a GL 3.3 core context on Mesa's EGL "orbis" platform (one 1920x1080 surface, vsync);
  desktop GL entry points come from `eglGetProcAddress` (`platform/native_glad.c`).
- **Audio:** the game's 44.1 kHz stream is resampled to the 48 kHz `sceAudioOut` port.
- **Input:** `scePad`, presented to the game as one SDL gamepad.
- **Files:** the console has no working directory; `/data/ctr/` is the base path and the anchor of
  every relative path (orbis-compat), including `mkdir`/`rmdir` (`ps4/ps4_platform.c`).
- **Diagnostics:** crash handler, hang watchdog, driver log (`ps4/ps4_platform.c`).
- **Wide characters:** the SDK's `wmemchr` family works on 16-bit units while C++ `wchar_t` is
  32-bit; `ps4/ps4_wide.c` replaces them for the C++ parts of Mesa.
- Changes to the game itself: the SDL build is skipped on PS4 (`CMakeLists.txt`), GL entry points
  from EGL (`platform/native_glad.c`), no `chdir` (`main.c`), plus one 64-bit fix that also applies to
  PC: the modern minimap stored heap pointers in 32-bit fields (`platform/native_minimap.c`).

## Building

Windows + Git Bash, with LLVM 18, CMake, Ninja, the [orbis-sdk-v1](https://github.com/orbis-ports/orbis-porting-kit/releases/tag/orbis-sdk-v1)
bundle and the OpenOrbis 0.5.4 toolchain (for its sample modules):

```bash
cmake -S . -B ../build-ps4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=<orbis-sdk-v1>/toolchain/orbis-sdk.cmake \
      -DCTR_NATIVE_64BIT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build ../build-ps4 --target ctr_native
ORBIS_SDK=<orbis-sdk-v1> OPENORBIS=<OpenOrbis PS4Toolchain> bash ps4/package.sh ../build-ps4 ../out
```

The package uses the OpenOrbis samples' layout and signature (paid `0x3800000000000011`, default
authinfo, `sce_module/libc.prx` + `libSceFios2.prx`): on GoldHEN that layout starts and gets the
full ~4.4 GiB of memory, which the GPU driver needs.

## Not affiliated, and built with AI

- This port is not affiliated with or endorsed by the Turbocharged / High Octane developers,
  Activision, Naughty Dog or Sony. Crash Team Racing is a trademark of its owners.
- The PS4 work was written with heavy use of an AI assistant (Anthropic's Claude), directed and
  tested on the console by [@alechurri](https://github.com/alechurri). SDL's policy forbids
  AI-generated code in contributions to SDL; nothing here modifies SDL or is submitted to it.

## Credits

The CTR decompilation (CTR-tools), ctr-native, High Octane by Rinnegatamante and Crash Team Racing:
Turbocharged by Cameron Redmore and contributors (the game code); orbis-ports (orbis-compat and the
PS4 Mesa driver); OpenOrbis (toolchain); SDL (public headers).
