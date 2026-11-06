#!/usr/bin/env bash
# Builds the Deskstrate keymap for the Keychron Q3 Max on top of Keychron's QMK fork.
#   firmware/keyboard/build.sh   (the UK ISO + encoder board, the only one in use)
# Needs the QMK build environment: QMK MSYS on Windows, or arm-none-eabi-gcc, dfu-util
# and `pip install qmk` elsewhere. Images land in out/.
set -euo pipefail

# Keychron's fork, branch 2025q3, pinned so builds are repeatable.
KEYCHRON_REPO=https://github.com/Keychron/qmk_firmware
KEYCHRON_COMMIT=57e682caa0a821e6de1b589e6f983f266151ab02

here=$(cd "$(dirname "$0")" && pwd)
qmk_dir=$here/.qmk
variants=(iso_encoder)

if [ "$(git -C "$qmk_dir" rev-parse HEAD 2>/dev/null)" != "$KEYCHRON_COMMIT" ]; then
    rm -rf "$qmk_dir"
    git init -q "$qmk_dir"
    git -C "$qmk_dir" remote add origin "$KEYCHRON_REPO"
    git -C "$qmk_dir" fetch -q --depth 1 origin "$KEYCHRON_COMMIT"
    git -C "$qmk_dir" checkout -q FETCH_HEAD
    git -C "$qmk_dir" submodule update -q --init --recursive --depth 1 \
        lib/chibios lib/chibios-contrib lib/printf
fi

# Keychron's Q3 Max config scans only 9 phases (CB1-CB9) of its SNLED27351 LED drivers,
# but its LED tables put the Tab row on driver 0's CB10-CB12, so that row stays dark.
# Scan all 12. (A keymap config.h can't override it: it is read before the keyboard's.)
sed -i 's/SNLED27351_SCAN_PHASE_9_CHANNEL/SNLED27351_SCAN_PHASE_12_CHANNEL/' \
    "$qmk_dir/keyboards/keychron/q3_max/config.h"

# VIA stamps the EEPROM with magic bytes taken from the build date, and wipes the keymap
# and macros at boot when they don't match, so every new build used to reset them. Use a
# fixed date instead. Bump it only when the stored layout changes (layer or macro sizes,
# or a new KEYCHRON_COMMIT that moves things), which resets the keymap and macros once.
VIA_EEPROM_DATE=2026-11-06-00:00:00
sed -i -E "s/(char \*  p      = )[^;]*;/\1\"$VIA_EEPROM_DATE\";/" "$qmk_dir/quantum/via.c"
[ "$(grep -c "\"$VIA_EEPROM_DATE\";" "$qmk_dir/quantum/via.c")" = 2 ] ||
    { echo "build.sh: failed to patch the VIA EEPROM magic in quantum/via.c" >&2; exit 1; }

mkdir -p "$here/out"
for v in "${variants[@]}"; do
    km=$qmk_dir/keyboards/keychron/q3_max/$v/keymaps/deskstrate
    rm -rf "$km"
    mkdir -p "$km"
    cp "$qmk_dir/keyboards/keychron/q3_max/$v/keymaps/keychron/keymap.c" "$km/"
    cp "$here"/keymap/* "$km/"
    cp "$here/../../components/kb_link_proto/include/kb_link_proto.h" "$km/"
    target=keychron/q3_max/$v:deskstrate
    make -C "$qmk_dir" "$target"
    cp "$qmk_dir/keychron_q3_max_${v}_deskstrate.bin" "$here/out/"
done
ls -l "$here/out"
