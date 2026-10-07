# Installing Crash Team Racing: Turbocharged on PS4

A step-by-step guide, from your game disc to playing on the console. It takes about 20 minutes the
first time; most of it is dumping the disc.

> **In short:** dump your own US disc to a `.bin` file → copy it to `/data/ctr/assets/ctr-u.bin` on
> the PS4 over FTP **in binary mode** → install the `.pkg` with GoldHEN → play.

## What you need

| | Notes |
| --- | --- |
| A PS4 with **GoldHEN** | Tested on a PS4 Pro, firmware 12.02. Other models and firmwares are untested. |
| Your own **Crash Team Racing disc, US version (NTSC-U)** | European (PAL) and Japanese discs are not supported. No game data comes with this port. |
| A PC with a **CD/DVD drive** | To dump the disc. An external USB drive works. |
| **ImgBurn** (Windows, free) | To dump the disc. redumper or DiscImageCreator also work. |
| **FileZilla** (free) | To copy files to the PS4 over FTP. |
| The package `ctr-turbocharged-ps4-v0.1.2.pkg` | From the [Releases page](https://github.com/alechurri/Crash-Team-Racing-Turbocharged-ps4/releases): download the `.pkg` of the latest release (the `.elf` and `SHA256SUMS` files are only for bug reports). |

## Step 1: dump your disc on the PC

The game reads its data (levels, sounds, music, videos) from an exact copy of the disc. It has to be
a **raw** copy: a normal `.iso` file does not work, because it leaves out the music and video data.

1. Put the Crash Team Racing disc in the PC's drive.
2. Open ImgBurn and choose **Create image file from disc**.
3. Pick the drive as the source and a folder on your PC as the destination, and press the big
   read button.
4. ImgBurn writes two files: a `.bin` (several hundred MB) and a small `.cue`. **You only need the
   `.bin`.** If ImgBurn offers to save an `.iso`, change the type to `.bin`.
5. Rename the `.bin` to **`ctr-u.bin`** (all lowercase).

> **Optional check:** if you also play the PC version of Turbocharged, select your `.bin` in its
> first-run setup. It checks that the dump is a complete US disc and keeps a verified copy in its
> `assets` folder as `ctr-u.bin`: that copy is the safest one to put on the PS4.

## Step 2: connect to the PS4 with FileZilla

1. On the PS4, with GoldHEN running, go to **Settings → GoldHEN → Server Settings** and enable
   the **FTP Server**. Note the IP address it shows (or find it in *Settings → Network → View
   Connection Status*).
2. On the PC, open FileZilla and connect: **Host** = the PS4's IP, **Port** = `2121`, user name
   and password empty, then **Quickconnect**.
3. **Set binary mode before copying anything:** menu **Transfer → Transfer type → Binary**.
   In *Auto* or *ASCII* mode FileZilla can silently damage the files, and the game will not start or
   will crash. Other FTP programs: look for the same setting; avoid WinSCP, which has damaged files
   for PS4 ports before.

## Step 3: copy your disc image

Do this **before** installing and starting the game, so the first start already finds it.

1. In FileZilla's right-hand panel (the PS4), open the folder **`/data`**.
2. Create a folder **`ctr`** inside it (right click → *Create directory*), and inside `ctr` a folder
   **`assets`**: you end up with `/data/ctr/assets/`.
3. Upload `ctr-u.bin` into `/data/ctr/assets/` (in binary mode, step 2). The final path must be
   exactly **`/data/ctr/assets/ctr-u.bin`**.

## Step 4: install the package

1. Upload `ctr-turbocharged-ps4-v0.1.2.pkg` to **`/data/pkg/`** on the PS4 (create the folder if it
   does not exist), also in binary mode. A USB stick works too: put the `.pkg` in its root folder.
2. On the PS4: **Settings → GoldHEN → Package Installer** (or *Settings → Debug Settings → Game →
   Package Installer*), pick the package and install it.
3. **CTR Turbocharged** appears on the home screen.

## Step 5: play

1. Start **CTR Turbocharged**. The first start takes a few extra seconds: it copies its fonts to
   `/data/ctr/assets/` and prepares graphics data.
2. The game may ask you to choose a **language** and a **settings preset** (PS1, Vanilla+,
   Turbocharged or Custom; you can change it later in the options). **Choose English**: the other
   languages come from the European version of the game and show some text wrong (see
   Troubleshooting).
3. To **quit**, use the game's own menu: the console returns to the home screen.

### Controls

The DualShock 4 works like the original PlayStation controller: **Cross, Circle, Square, Triangle,
L1, R1, L2 and R2 are the same buttons** the game always used, the **D-pad or left stick** steers,
**Options** is Start and a **touch pad click** is Select. Every button can be remapped in the game's
options.

## Your files on the console

Everything the game writes lives in **`/data/ctr/`**:

| Path | What it is |
| --- | --- |
| `assets/ctr-u.bin` | your disc image |
| `memcards/` | **your saves** (memory cards). Copy this folder to your PC to back them up. |
| `config.ini` | your settings |
| `ps4.log`, `mesa.log`, `Crash Team Racing- Turbocharged.log` | logs of the last run (for bug reports) |

**Updating:** install the new package over the old one (step 4). Your saves, settings and disc
image are kept.

**Uninstalling:** delete *CTR Turbocharged* from the home screen (Options button → Delete), and
delete `/data/ctr/` over FTP if you also want to remove your saves and disc image.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| The game closes right after starting | The disc image is missing, misnamed or not a raw US dump. Check that `/data/ctr/assets/ctr-u.bin` exists with exactly that name. `/data/ctr/ps4.log` says `disc image MISSING` if it was not found; the game's own log (`Crash Team Racing- Turbocharged.log`) says why a found image was rejected. Dump it again if needed (step 1) and copy it in binary mode. |
| The package does not install, or the game crashes at once with a corrupted-looking file | A file was damaged during the copy. Upload it again with FileZilla in **binary** mode. |
| "1&" instead of "1st", or text overflowing a box | The language is not English. Switch the language to English in the game's options. (The US game was only made for English; this happens on PC too.) |
| "Memory card slot full" when saving | You are on v0.1.0: install the latest version. |
| Error CE-34878-0 when quitting | You are on v0.1.0 or v0.1.1: install the latest version. |
| Anything else | [Open an issue](https://github.com/alechurri/Crash-Team-Racing-Turbocharged-ps4/issues) with your console model and firmware, what you did, and the three logs from `/data/ctr/` (download them before starting the game again: each start replaces them). |

Please report PS4 problems in this repository, not to the upstream Turbocharged project.
