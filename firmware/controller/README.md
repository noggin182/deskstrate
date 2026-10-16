# Controller networking

The controller joins a 2.4 GHz Wi-Fi network in station mode and sends UDP
broadcast datagrams to `255.255.255.255:3333`. Configure the SSID and password
with PlatformIO's `menuconfig` target (`platformio run -e
adafruit_feather_esp32s3_tft -t menuconfig`) under **Deskstrate Controller**.
The values are stored in the local ESP-IDF `sdkconfig`; do not commit Wi-Fi
credentials.

The ESP-IDF console is routed over the Feather's native USB Serial/JTAG port,
so `printf()` and ESP-IDF log messages appear in PlatformIO's monitor on that
USB port (115200 baud).

After connecting or reconnecting, the controller broadcasts this JSON status
datagram:

```json
{"type":"status","event":"online"}
```

Receivers should listen on UDP port `3333` on all local interfaces. Other
controller updates can be sent from firmware code with
`network_broadcast_update()` in `src/network.h`. Pass a JSON string; each call
sends it as one UDP datagram, up to 1400 bytes. The function returns
`ESP_ERR_INVALID_STATE` when Wi-Fi/UDP is not ready and reports socket send
errors in the ESP-IDF log. UDP broadcast is limited to the local network;
router client isolation or firewall rules may prevent devices from receiving
each other's broadcasts.

The NeoKey switches use Feather D5 (Menu/Back), D6 (A), D10 (Function/split),
and D11 (B), configured as active-low inputs with internal pull-ups. The four
chained NeoPixels share D9 and animate through the color wheel; pressing any
key shifts the animation colors.

## Reserved KVM and monitor-control pins

The following GPIOs are reserved for future KVM SWD control and separate DDC
I2C buses for the two monitors. The corresponding named defines are present in
`src/main.cpp`, but the pins are not configured or used by firmware yet.

| Function | Feather pin | ESP32-S3 GPIO |
| --- | --- | --- |
| KVM 1 SWDIO | A0 | GPIO18 |
| KVM 1 SWCLK | A1 | GPIO17 |
| KVM 2 SWDIO | A2 | GPIO16 |
| KVM 2 SWCLK | A3 | GPIO15 |
| Monitor 1 DDC SCL | A4 | GPIO14 |
| Monitor 1 DDC SDA | A5 | GPIO8 |
| Monitor 2 DDC SCL | RX | GPIO2 |
| Monitor 2 DDC SDA | TX | GPIO1 |

Keep the monitors' DDC buses separate. Use the installed bidirectional
level-shifters between each monitor's DDC lines and the 3.3 V Feather GPIOs;
ESP32-S3 GPIOs are not 5 V tolerant. Connect controller and device grounds
together, and verify the KVM SWD voltage levels before connecting those pins.
The Feather's USB Serial/JTAG console remains in use; RX/TX here refer to its
header GPIO pads.

## Status screen and controls

The TFT status screen is portrait (135 x 240). Its routing diagram shows the
host routed to each monitor (PC, Mac, Guest 3, or Guest 4) and the assigned
host for each hub. USB-C, DP, and HDMI remain internal monitor input state and
are represented by input-specific icons inside each monitor: an Apple-style
mark for USB-C, a Windows mark for DP, and an HDMI connector for HDMI.
Matching monitor-to-hub assignments are shown with routes; mismatched
assignments remain unconnected. Hub names are represented by keyboard and
camera icons rather than labels. This is a UI demo and does not switch the
physical KVM. Companion-app volume data is not connected yet, so volume
readings remain `--%`.

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

The four keys directly cycle the preview state: Menu/Back (top left) advances
Monitor 1's input, Function/split (top right) advances Monitor 2's input, A
(bottom left) advances Hub 1's host, and B (bottom right) advances Hub 2's
host. Monitor inputs cycle DP, USB-C, HDMI; hub hosts cycle PC, Mac, Guest 3,
Guest 4. Each key changes only its corresponding value and wraps to the first
value after the last. A temporary code overlay covers the volume panel to make
states easy to report: its digits are ordered Monitor 1, Monitor 2, Hub 1,
Hub 2. Monitor input digits are DP=1, USB-C=2, HDMI=3; hub host digits are
PC=1, Mac=2, Guest 3=3, Guest 4=4. For example, `2211` means both monitors
are on USB-C and both hubs are assigned to PC. This is a UI demo and does not
switch the physical KVM; the planned switching and menu behaviors are not
implemented yet.
