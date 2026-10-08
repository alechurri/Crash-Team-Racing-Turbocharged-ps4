# Crash Team Racing: Turbocharged for PS4

A port of [Crash Team Racing: Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged)
to jailbroken PlayStation 4 consoles. Turbocharged is a PC version of Crash Team Racing rebuilt from
the decompiled game code; this port runs it natively on the console, without an emulator, at 60 fps.

<img src="screenshots/game1.jpg" alt="Crash Team Racing: Turbocharged"><br>
<sub>Screenshot of Turbocharged on PC; the PS4 port uses the same renderer.</sub>

## Get started

1. **[Download the latest `.pkg`](https://github.com/alechurri/Crash-Team-Racing-Turbocharged-ps4/releases/latest)**
   (only the `.pkg`; the `.elf` is for bug reports).
2. **Follow the [step-by-step install guide](INSTALL.md).** In short: dump **your own US (NTSC-U)
   disc** as a raw `.bin` → copy it to `/data/ctr/assets/ctr-u.bin` over FTP **in binary mode** →
   install the `.pkg` with GoldHEN → play.

No game data is included: you need your own Crash Team Racing disc.

## Status

**v0.1.2** (tested on a PS4 Pro, firmware 12.02, GoldHEN): the game boots and plays with graphics,
sound, the DualShock 4 and memory card saves working, and quitting from the game's menu returns to
the home screen. Not every mode and option has been checked one by one yet.

**Known issues**
- Set the language to **English**. In Spanish (and probably the other European languages) some text
  shows `&` instead of the ordinal ("1&" for "1º") and some HUD labels overflow their boxes. It comes
  from the US version of the game and happens on PC too.
- Closing the game from the PS button menu (*Close Application*) is untested; quit from the game's
  own menu instead.

## Controls

The DualShock 4 works like the original PlayStation controller: same face buttons, L1/R1, L2/R2,
D-pad or left stick to steer, **Options** = Start, **touch pad click** = Select. Everything can be
remapped in the game's options.

For the game's features and options (settings presets, fonts, rendering options), see the
[Turbocharged README](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged#readme):
the PS4 version has the same menus.

## Problems?

Check the [troubleshooting table](INSTALL.md#troubleshooting) first. If that does not help,
[open an issue](https://github.com/alechurri/Crash-Team-Racing-Turbocharged-ps4/issues) with your
console model and firmware, what happened, and the logs from `/data/ctr/` (`ps4.log`, `mesa.log`
and `Crash Team Racing- Turbocharged.log`; copy them before starting the game again, since each
start replaces them).

Report PS4 problems here, not to the upstream Turbocharged project.

## For developers

How the port works and how to build it: [ps4/README.md](ps4/README.md). All PS4-specific code is in
the [`ps4/`](ps4) folder; the rest of the repository is the Turbocharged game code.

## Not affiliated, and built with AI

- This port is not affiliated with or endorsed by the Turbocharged / High Octane developers,
  Activision, Naughty Dog or Sony. Crash Team Racing is a trademark of its owners.
- The PS4 work was written with heavy use of an AI assistant (Anthropic's Claude), directed and
  tested on the console by [@alechurri](https://github.com/alechurri).

## Credits and license

The game code: the CTR decompilation ([CTR-tools](https://github.com/CTR-tools)),
[ctr-native](https://github.com/CTR-tools/ctr-native),
[High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) by Rinnegatamante and
[Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged) by Cameron Redmore
and contributors. PS4 platform: [orbis-ports](https://github.com/orbis-ports) (orbis-compat and the
PS4 Mesa driver) and [OpenOrbis](https://github.com/OpenOrbis) (toolchain).

GPL-3.0, like the game it ports ([LICENSE](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md)).
