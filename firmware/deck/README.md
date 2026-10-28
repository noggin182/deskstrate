# Deskstrate deck

The deck is the desk unit: an Adafruit Feather ESP32-S3 TFT with two NeoKey
FeatherWings. It is a client of the core (`firmware/core`), which owns all
switching state.

Build and flash with `pio run -d firmware/deck -t upload`.

The deck has two OTA slots and rollback, and updates itself from GitHub releases
(doorbell on UDP 18211, or hourly; see Deployment in the top README).
It has no push endpoint: USB is the fallback. A new image is confirmed once the
UI is up and Wi-Fi has an IP, otherwise it rolls back.

## Networking

The deck joins a 2.4 GHz Wi-Fi network in station mode and sends UDP
broadcast datagrams to `255.255.255.255:3333`, with the DHCP hostname
`deskstrate-deck`. The Wi-Fi credentials live in the deck's NVS. To provision a
new board (or one whose NVS was erased), set the SSID and password with
PlatformIO's `menuconfig` target (`pio run -d firmware/deck -t menuconfig`)
under **Deskstrate Wi-Fi** and flash once. After that, builds with them blank
(fresh checkouts, CI) keep using the stored ones. The local
`firmware/deck/sdkconfig.adafruit_feather_esp32s3_tft` is gitignored; do not
commit Wi-Fi credentials.

The ESP-IDF console is routed over the Feather's native USB Serial/JTAG port,
so `printf()` and ESP-IDF log messages appear in PlatformIO's monitor on that
USB port (115200 baud).

After connecting or reconnecting, the deck broadcasts this JSON status
datagram:

```json
{"type":"status","event":"online"}
```

The deck is also a `kvm_proto` client of the core on UDP port 18200
(`KVM_PORT`). It renders every `STATE` the core broadcasts and sends its key
presses to the core as `REQUEST`s (to the sender of the last `STATE`, or
broadcast until one has been seen). Packets are unauthenticated for now.

This JSON broadcast is temporary and will be dropped in favour of `kvm_proto`.
Receivers should listen on UDP port `3333` on all local interfaces. Other
deck updates can be sent from firmware code with
`network_broadcast_update()` in `src/network.h`. Pass a JSON string; each call
sends it as one UDP datagram, up to 1400 bytes. The function returns
`ESP_ERR_INVALID_STATE` when Wi-Fi/UDP is not ready and reports socket send
errors in the ESP-IDF log. UDP broadcast is limited to the local network;
router client isolation or firewall rules may prevent devices from receiving
each other's broadcasts.

The NeoKey switches use Feather D5 (top left), D6 (bottom left), D10 (top right),
and D11 (bottom right), configured as active-low inputs with internal pull-ups.
The four chained NeoPixels share D9; what they show is described under Status
screen and controls.

The Feather's built-in NeoPixel (GPIO33) is the status LED: see Status LED in the
top README. It turns bright orange when no `STATE` has come from the core for 10 s.

## Status screen and controls

The TFT status screen is portrait (135 x 240). Its routing diagram shows the
host routed to each monitor (PC, Mac, Guest 3, or Guest 4) and the assigned
host for each hub. USB-C, DP, and HDMI remain internal monitor input state and
are represented by input-specific icons inside each monitor: an Apple-style
mark for USB-C, a Windows mark for DP, and an HDMI connector for HDMI.
Matching monitor-to-hub assignments are shown with routes; mismatched
assignments remain unconnected. Hub names are represented by keyboard and
camera icons rather than labels. Companion-app volume data is not connected yet, so volume
readings remain `--%`. The diagram shows the state reported by the core; until
the first `STATE` arrives it shows a placeholder.

The HDMI input on Monitor 1 is Guest 3; HDMI on Monitor 2 is Guest 4. Hub 1
carries the mouse and keyboard; Hub 2 carries the webcam and headset. The
keyboard and camera icons use a joint, split, or split-and-swapped layout:
they are grouped around a slash when assigned to the same host; otherwise
they sit under opposite monitor columns. Use the split-and-swapped layout if
the camera host matches Monitor 1, or if Monitor 1 matches neither hub and
the keyboard host matches Monitor 2. When both monitors and both hubs share a
host, each monitor has its own short drop into the shared bus. The shared bus
sits higher when both monitors match than when only one does. Matching
monitor and hub routes are connected; other routes remain unconnected. Route
connectors leave a four-pixel gap before the monitors and hub icons, and
resume four pixels below each hub icon. PC is orange, Mac is blue, and the
guest hosts are purple and coral. Host labels appear below the hub icons,
including guest numbers. Volume is planned for PC and Mac only and depends on
a companion app running on those hosts.

The keys ask the core to switch (`src/controls.h`). Split mode is not stored on
the deck: it is whenever the two monitors show different hosts.

| Keys | Outside split mode | In split mode |
| --- | --- | --- |
| Bottom left | everything (both monitors, both hubs) to the PC | keyboard/mouse hub to the left screen's host |
| Bottom right | everything to the Mac | keyboard/mouse hub to the right screen's host |
| Hold top right, tap bottom left | split: PC left, Mac right, both hubs on the PC | same |
| Hold top left, tap bottom right | split: Mac left, PC right, both hubs on the Mac | same |
| Tap top right | nothing | leave split: everything to the keyboard hub's host |
| Tap top left | open the menu | open the menu |

Top keys act on release, and only when no bottom key was tapped while they were
held (and they were held under 1.5 s). Bottom keys act on press.

In the menu (`src/menu.h`) top left is back, top right is select, and bottom
left/right move up/down. Its items set each monitor's input (DP, USB-C, HDMI)
and each hub's host (PC, Mac, Guest 3, Guest 4), swap the two screens, put both
screens in standby, and set the key light level (High, Low, Off; kept in NVS).
The firmware version is shown at the bottom. The menu closes after 20 s without
a key press.

The key lights (`src/neokeys.h`) each animate the host they stand for, in that
host's screen colour: the top keys show the host on the monitor above them
(nearly dark when it is in standby), and the bottom keys the host they send the
keyboard to, fully animated while the keyboard/mouse hub is on it and a dim glow
otherwise. The PC breathes, the Mac ripples between blue and cyan, and guests
beat twice. While a top key is held, the bottom key it chords with blinks white.
In the menu the keys show back (red), select (green) and up/down (white). Until
the first `STATE` arrives all four breathe amber. Each press flashes its key
white. Which NeoPixel sits under which key is set in `kPixelForKey` in
`src/neokeys.cpp`.

A temporary code overlay covers the volume panel to make
states easy to report: its digits are ordered Monitor 1, Monitor 2, Hub 1,
Hub 2. Monitor input digits are DP=1, USB-C=2, HDMI=3; hub host digits are
PC=1, Mac=2, Guest 3=3, Guest 4=4. For example, `2211` means both monitors
are on USB-C and both hubs are assigned to PC.
