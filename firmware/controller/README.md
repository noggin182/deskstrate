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

The NeoKey FeatherWing uses Feather D5 and D6 for its active-low switches and
D9 for its two chained NeoPixels. The pixels animate through the color wheel;
pressing either key shifts the animation colors.

## Status screen and controls

The TFT status screen is portrait (135 x 240). Its routing diagram shows the
host routed to each monitor (PC, Mac, Guest 3, or Guest 4) and the assigned
host for each hub. USB-C, DP, and HDMI remain internal monitor input state and
are not rendered as labels. Hubs matching a monitor host draw a route from the
matching display(s); if both displays share the host, their routes join into a
bus. A hub host not used by either display gets its own node below. Hub names
are represented by keyboard and headset icons rather than labels. Each NeoKey
press randomizes the displayed scenario to preview different combinations;
this is only a UI demo and does not switch the KVM. Companion-app volume data
is not connected yet, so volume readings remain `--%`.

The HDMI monitor input is represented as Guest 3 in the route diagram. Hub 1
carries the mouse and keyboard; Hub 2 carries the webcam and headset. Volume is
planned for PC and Mac only and depends on a companion app running on those
hosts.

The four planned keys are A, B, Menu, and Function. In normal mode, A switches
to PC and B switches to Mac. Function enters split mode, assigning one monitor
to each of PC and Mac; in split mode, A and B select which host receives Hub 1.
Menu opens the menu, where A/B move up/down, Menu selects, and Function returns
one level. These controls and switching behaviors are not implemented yet;
the two available NeoKey switches currently randomize the displayed routing
scenario for UI testing.
