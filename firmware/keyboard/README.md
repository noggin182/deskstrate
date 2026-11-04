# Keyboard (Keychron Q3 Max)

QMK firmware for the Keychron Q3 Max, which gets an ESP32 inside it so the keyboard
can send Deskstrate requests itself. This folder holds only our additions. `build.sh`
fetches Keychron's QMK fork at a pinned commit into `.qmk/`, copies Keychron's stock
keymap plus `keymap/` in as a `deskstrate` keymap, and builds it, so everything else
behaves exactly like the stock firmware (VIA still works).

## Current state: pin test

The two spare STM32F401 pins the ESP32 will use are driven so they can be probed:

| Pin | LQFP64 pin | At power-up | After each Esc press |
| --- | --- | --- | --- |
| PB12 | 33 (bottom of the right-hand row) | high (3.3 V) | swaps |
| PA3 | 17 (left end of the bottom row) | low (0 V) | swaps |

The pin 1 mark is the chip's corner dimple by the white silkscreen dot; see the
annotated photo in the project files (keyboard/q3max-free-pins.jpg). Test in cable mode:
in wireless modes the keyboard sleeps when idle.

## Build and flash

`./build.sh` (both layouts) or `./build.sh iso_encoder`. CI (`bench-firmware.yml`)
builds both and uploads the `.bin` files as an artifact.

To flash: unplug, set the mode switch to Cable, hold Esc (or the reset button under the
space bar) while plugging in, then flash the `.bin` with QMK Toolbox or
`dfu-util -a 0 -d 0483:df11 -s 0x08000000:leave -D <file>.bin`. The STM32 bootloader
is in ROM, so a bad flash can always be redone, and Keychron's own firmware can be put
back the same way.

## KVM keys and VIA

`keymap/deskstrate_keys.h` adds KVM keys (everything to PC/Mac, the two split layouts, each monitor to
PC/Mac/guest, each hub to PC/Mac) in the custom keycode slots after Keychron's own.
Each builds a `kvm_proto` REQUEST; sending it needs the ESP32 link, which isn't built
yet, so for now pressing one does nothing.

They are bound like any other key, so rebinding doesn't need a reflash. To see them by
name, use VIA rather than the Keychron Launcher (which only loads Keychron's own
definitions): open usevia.app in Chrome or Edge with the keyboard on a cable, turn on
Settings > Show Design tab, load `via/q3_max_iso_encoder_deskstrate.json` (UK) or the
ANSI one under Design, and the keys appear under Custom. The definitions are generated
by `via/make_via_json.py` from Keychron's `keyboard.json`, with the lighting menu
copied from the Launcher's own definition; rerun it if the keys change.
