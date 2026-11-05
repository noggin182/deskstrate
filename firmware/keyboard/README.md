# Keyboard (Keychron Q3 Max)

QMK firmware for the Keychron Q3 Max, which gets an ESP32 inside it so the keyboard
can send Deskstrate requests itself. This folder holds only our additions. `build.sh`
fetches Keychron's QMK fork at a pinned commit into `.qmk/`, copies Keychron's stock
keymap plus `keymap/` in as a `deskstrate` keymap, and builds it, so everything else
behaves exactly like the stock firmware (VIA still works).

## Link to the ESP32

Two spare STM32F401 pins carry a framed UART to the ESP32 inside the keyboard
(`firmware/kb`); the format is in `components/kb_link_proto/include/kb_link_proto.h`,
which `build.sh` copies in. `keymap/kb_link.c` is this side of it.

| Pin | LQFP64 pin | Use |
| --- | --- | --- |
| PB12 | 33 (bottom of the right-hand row) | keyboard → ESP. No UART on this pin, so it is bit-banged at 57600 baud from the DWT cycle counter, with interrupts masked for each byte (~175 µs). Held high (idle) in cable mode with USB power, low otherwise: the ESP sleeps while it is low. |
| PA3 | 17 (left end of the bottom row) | ESP → keyboard: USART2 RX into a circular DMA buffer (DMA1 stream 5), read every matrix scan. |

The pin 1 mark is the chip's corner dimple by the white silkscreen dot; see the
annotated photo in the project files (keyboard/q3max-free-pins.jpg).

The keyboard sends a hello and then a status frame every 2 s (transport, USB power,
detected host OS, base layer, battery, link counters), which the ESP shows on its page.
A KVM key sends its request and resends it every 50 ms until the ESP acks it (5 tries).
Text the ESP sends comes straight back as "echo: ...", to test the link both ways.

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
Each sends a `kvm_proto` REQUEST through the ESP32, which passes it to the core. That
works in cable mode with USB power only: in wireless modes the ESP32 is asleep and the
keys do nothing.

They are bound like any other key, so rebinding doesn't need a reflash. To see them by
name, use VIA rather than the Keychron Launcher (which only loads Keychron's own
definitions): open usevia.app in Chrome or Edge with the keyboard on a cable, turn on
Settings > Show Design tab, load `via/q3_max_iso_encoder_deskstrate.json` (UK) or the
ANSI one under Design, and the keys appear under Custom. The definitions are generated
by `via/make_via_json.py` from Keychron's `keyboard.json`, with the lighting menu
copied from the Launcher's own definition; rerun it if the keys change.

## Mac/Windows layout follows the host

On the cable, QMK's OS detection picks Keychron's Mac or Windows base layer each time
the keyboard enumerates, so it follows the hub when it switches hosts. On Bluetooth
and 2.4 GHz the switch on the back still decides. Flipping the switch while on the
cable takes effect until the next enumeration.
