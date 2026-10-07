#!/usr/bin/env bash
# Packages the PS4 build: build-ps4/ctr_native (ELF) -> out/IV0000-CTRT00001_00-CTRTURBOCHARGED0.pkg
#
#   bash ctr/ps4/package.sh [build dir] [output dir]
#   BUNDLE_DISC=/path/to/ctr-u.bin bash ctr/ps4/package.sh ...   (personal all-in-one, see below)
#
# Signed and laid out like OpenOrbis's own samples (paid 0x3800000000000011, default authinfo,
# sce_module/libc.prx + libSceFios2.prx, sce_sys/about/right.sprx, SFO category gd): the layout
# that, on a PS4 Pro with GoldHEN, both starts and gets the full ~4.4 GiB of direct memory (the GPU
# driver alone takes ~1 GiB). The game's own assets (fonts, the controller picture) travel in
# assets/; ps4/ps4_sdl.c copies them to /data/ctr/assets on the first start.
#
# Needs: the orbis-sdk-v1 bundle (SDK, with the Windows packaging tools in sdk/bin/windows), the
# OpenOrbis 0.5.4 toolchain (for the sample modules), Python 3. Paths below are the build
# machine's; set ORBIS_SDK and OPENORBIS to yours.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/.." && pwd)"
BUILD="${1:-$SRC/../build-ps4}"
OUT="${2:-$SRC/../out}"
SDK="${ORBIS_SDK:-C:/Users/alejo/eden-ps4/sdk-dl/orbis-sdk-v1}/sdk"
OO="${OPENORBIS:-C:/Users/alejo/soh-ps4/tools/OpenOrbis/OpenOrbis/PS4Toolchain}"
BIN="$SDK/bin/windows"
export DOTNET_ROLL_FORWARD=LatestMajor
export OO_PS4_TOOLCHAIN="$(cygpath -m "$SDK")"

TITLE="CTR Turbocharged"
TITLE_ID="CTRT00001"
CID="IV0000-${TITLE_ID}_00-CTRTURBOCHARGED0"
VERSION="$(tr -d '[:space:]' < "$SRC/VERSION")"
ELF="$BUILD/ctr_native"
ST="$BUILD/pkg-stage"

[ -f "$ELF" ] || { echo "missing $ELF"; exit 1; }
rm -rf "$ST"; mkdir -p "$ST/sce_sys/about" "$ST/sce_module" "$ST/assets/fonts" "$OUT"
python "$HERE/make_icon.py" "$ST/sce_sys/icon0.png"
"$BIN/create-fself.exe" -in="$(cygpath -m "$ELF")" -out="$(cygpath -m "$ST/x.oelf")" \
    --eboot "$(cygpath -m "$ST/eboot.bin")" --paid 0x3800000000000011 >/dev/null
rm -f "$ST/x.oelf"
cp "$OO/samples/piglet/sce_sys/about/right.sprx" "$ST/sce_sys/about/"
cp "$OO/samples/piglet/sce_module/libc.prx" "$OO/samples/piglet/sce_module/libSceFios2.prx" "$ST/sce_module/"
FILES="eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx"

# BUNDLE_DISC=<your raw BIN dump>: a personal all-in-one package with the disc image inside, read
# in place from /app0. It contains the game: for your own console only, never to share.
if [ -n "$BUNDLE_DISC" ]; then
    [ -f "$BUNDLE_DISC" ] || { echo "BUNDLE_DISC: no file at $BUNDLE_DISC"; exit 1; }
    cp "$BUNDLE_DISC" "$ST/assets/ctr-u.bin"
    FILES="$FILES assets/ctr-u.bin"
fi
cp "$SRC/assets/dualshock.png" "$ST/assets/"
FILES="$FILES assets/dualshock.png"
for f in "$SRC"/assets/fonts/*; do
    cp "$f" "$ST/assets/fonts/"
    FILES="$FILES assets/fonts/$(basename "$f")"
done

(
    cd "$ST"
    P="$BIN/PkgTool.Core.exe"; SFO=sce_sys/param.sfo
    "$P" sfo_new $SFO >/dev/null
    s() { "$P" sfo_setentry $SFO "$1" --type "$2" --maxsize "$3" --value "$4" >/dev/null; }
    s APP_TYPE Integer 4 1
    s APP_VER Utf8 8 "01.00"
    s ATTRIBUTE Integer 4 0
    s CATEGORY Utf8 4 gd
    s SYSTEM_VER Integer 4 0
    s CONTENT_ID Utf8 48 "$CID"
    s DOWNLOAD_DATA_SIZE Integer 4 0
    s TITLE Utf8 128 "$TITLE"
    s TITLE_ID Utf8 12 "$TITLE_ID"
    s VERSION Utf8 8 "01.00"
    "$BIN/create-gp4.exe" -out pkg.gp4 --content-id="$CID" --files "$FILES" >/dev/null
    "$P" pkg_build pkg.gp4 "$(cygpath -m "$OUT")" >/dev/null
)
if [ -n "$BUNDLE_DISC" ]; then
    mv -f "$OUT/$CID.pkg" "$OUT/$CID-ALL-IN-ONE-PERSONAL.pkg"
    echo "personal all-in-one package (contains your disc image: do not share it)"
fi
echo "game version $VERSION"
ls -la "$OUT"
