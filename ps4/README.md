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

**v0.1.2:** on the console the game boots and plays, with graphics, sound, the DualShock 4 and
memory card saves working, and quitting from the game's menu returns to the PS4 home screen
without an error (tested on a PS4 Pro). Not every mode and option has been checked one by one yet.

- v0.1.2: quitting no longer shows error CE-34878-0 (the app asks the system to close it instead of
  returning from `main()`); the crash handler, driver log and fps line in `ps4.log` now actually start.
- v0.1.1: saves work (before, the game said the memory card slot was full).
- v0.1.0: first release; everything but saving worked.

**Known issues**
- With the language set to **Spanish** (and probably the other European languages), some text
  shows `&` instead of the ordinal ("1&" for "1º"), and some HUD labels such as "VUELTA 1 TIEMPO"
  overflow their boxes. This comes from the US version of the game, which was only made for
  English, and happens on the PC version of Turbocharged too; it is not specific to the PS4. English
  shows correctly.
- Closing the game from the PS button menu (*Close Application*) has not been tested yet; quit from
  the game's own menu instead.

## You need

| File | Where it comes from | Where it goes on the PS4 |
| --- | --- | --- |
| `ctr-u.bin` | **your own** NTSC-U (US) Crash Team Racing disc, dumped as a raw BIN (below) | `/data/ctr/assets/ctr-u.bin` |
| `ctr-turbocharged-ps4-v0.1.2.pkg` | the [Releases page](https://github.com/alechurri/Crash-Team-Racing-Turbocharged-ps4/releases) | installed as a package |

- A PS4 with GoldHEN (only tested on a PS4 Pro, firmware 12.02).
- No game data is included or provided.

**Dumping your disc.** Put the disc in a PC drive and make a **raw MODE2/2352** image: in ImgBurn,
*Create image file from disc*; it saves a `.bin` and a `.cue`, and the `.bin` is the one you need
(redumper or DiscImageCreator also work). A 2048-byte `.iso` does **not** work: it lacks the audio
and video sectors. If you also run the PC version of Turbocharged, its first-run setup checks the
dump and keeps a verified copy as `assets/ctr-u.bin`: that copy is the safest one to use here.

## Install

Do the steps in this order: the disc image goes on the console **before** the game is started, so
the first start already finds it.

1. **Copy the disc image.** On the PS4, enable GoldHEN's FTP server (*Settings → GoldHEN → Server
   Settings*). In FileZilla connect to the console's IP, port **2121**, set *Transfer → Transfer
   type → Binary*, create the folders `/data/ctr/assets/` and upload your image there renamed to
   exactly **`ctr-u.bin`** (lowercase): `/data/ctr/assets/ctr-u.bin`.
2. **Install the package.** Upload `ctr-turbocharged-ps4-v0.1.2.pkg` to `/data/pkg/`, also in
   **binary mode** (or put it on a USB drive), and install it from *Settings → GoldHEN → Package Installer*.
3. **Play.** Start **CTR Turbocharged** from the home screen. The first start copies the game's
   fonts into `/data/ctr/assets/`.

**Updating:** install the new package over the old one. Saves (`memcards/`), settings
(`config.ini`) and your disc image live in `/data/ctr/` and are kept.

## Troubleshooting

All logs are in `/data/ctr/`; download them before starting the game again.

- **The game closes right after starting.** The disc image is missing or not a valid raw NTSC-U
  dump. `ps4.log` says where it looked: `disc image MISSING` means there is no
  `/data/ctr/assets/ctr-u.bin` (check the folder and the exact file name); the game's own log
  (`Crash Team Racing- Turbocharged.log`) says why a present image was rejected.
- **"Memory card slot full" when saving.** That was v0.1.0; install the latest version.
- **Error CE-34878-0 when quitting.** That was v0.1.0 and v0.1.1; install the latest version.
- **"1&" instead of "1st", or text overflowing a box.** The Spanish language setting; see *Known
  issues* above. Switch the language to English in the options.
- **Anything else.** Open an issue with `ps4.log`, `mesa.log` and the game's log, your console model
  and firmware.

## Controls

The DualShock 4 works as a standard gamepad: Cross, Circle, Square and Triangle as on the PS1,
L1/R1, L2/R2, sticks and D-pad, Options = Start, touch pad click = Select. The game's own options
menu remaps them.

## Logs (for bug reports)

All in `/data/ctr/`:

- `ps4.log`: the PS4 layer: startup, GL context, controller, audio, an fps line every 10 s, and on a
  crash the registers and a backtrace as `eboot+0x...` offsets (symbolize them with the release's
  ELF: `llvm-symbolizer --obj=ctr-turbocharged-ps4-v0.1.2.elf -C -f 0x<offset>`). The previous run's
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
