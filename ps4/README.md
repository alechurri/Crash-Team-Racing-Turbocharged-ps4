# PS4 port: how it works and how to build it

Technical notes for developers. **Players: see the [main README](../README.md) and the
[install guide](../INSTALL.md).**

| | |
|---|---|
| Based on | the `64bit-2026.10` branch of Turbocharged (the experimental 64-bit build; the PS4 only runs 64-bit code) |
| Graphics | the game's PC renderer (OpenGL 3.3 core) on Mesa: EGL → zink → RADV → video out, from the [orbis-ports](https://github.com/orbis-ports) SDK |
| PS4 code | everything in this folder; the game itself has only a few small `#ifdef`s (listed below) |

## How the port works

- **No SDL on the console.** Upstream SDL3 has no PS4 support, and SDL does not accept AI-generated
  code, so this port does not add a backend to it. `ps4/ps4_sdl.c` implements the 88 SDL3
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
  from EGL (`platform/native_glad.c`), no `chdir` (`main.c`), the disc image also read from `/app0/assets/ctr-u.bin` for personal
  all-in-one packages (`platform/native_assets.c`, `platform/native_disc_image.c`), plus one 64-bit fix that also applies to
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

## Logs

All in `/data/ctr/`:

- `ps4.log`: the PS4 layer: startup, GL context, controller, audio, an fps line every 10 s, and on a
  crash the registers and a backtrace as `eboot+0x...` offsets (symbolize them with the release's
  ELF: `llvm-symbolizer --obj=ctr-turbocharged-ps4-v0.1.2.elf -C -f 0x<offset>`). The previous run's
  is kept as `ps4.old.log`.
- `mesa.log`: the GPU driver.
- `Crash Team Racing- Turbocharged.log`: the game's own log.
