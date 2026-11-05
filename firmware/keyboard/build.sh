#!/usr/bin/env bash
# Builds the Deskstrate keymap for the Keychron Q3 Max on top of Keychron's QMK fork.
#   firmware/keyboard/build.sh [ansi_encoder|iso_encoder ...]   (default: both)
# Needs the QMK build environment: QMK MSYS on Windows, or arm-none-eabi-gcc, dfu-util
# and `pip install qmk` elsewhere. Images land in out/.
set -euo pipefail

# Keychron's fork, branch 2025q3, pinned so builds are repeatable.
KEYCHRON_REPO=https://github.com/Keychron/qmk_firmware
KEYCHRON_COMMIT=57e682caa0a821e6de1b589e6f983f266151ab02

here=$(cd "$(dirname "$0")" && pwd)
qmk_dir=$here/.qmk
variants=("$@")
[ ${#variants[@]} -gt 0 ] || variants=(ansi_encoder iso_encoder)

if [ "$(git -C "$qmk_dir" rev-parse HEAD 2>/dev/null)" != "$KEYCHRON_COMMIT" ]; then
    rm -rf "$qmk_dir"
    git init -q "$qmk_dir"
    git -C "$qmk_dir" remote add origin "$KEYCHRON_REPO"
    git -C "$qmk_dir" fetch -q --depth 1 origin "$KEYCHRON_COMMIT"
    git -C "$qmk_dir" checkout -q FETCH_HEAD
    git -C "$qmk_dir" submodule update -q --init --recursive --depth 1 \
        lib/chibios lib/chibios-contrib lib/printf
fi

mkdir -p "$here/out"
for v in "${variants[@]}"; do
    km=$qmk_dir/keyboards/keychron/q3_max/$v/keymaps/deskstrate
    rm -rf "$km"
    mkdir -p "$km"
    cp "$qmk_dir/keyboards/keychron/q3_max/$v/keymaps/keychron/keymap.c" "$km/"
    cp "$here"/keymap/* "$km/"
    target=keychron/q3_max/$v:deskstrate
    make -C "$qmk_dir" "$target"
    cp "$qmk_dir/keychron_q3_max_${v}_deskstrate.bin" "$here/out/"
done
ls -l "$here/out"
