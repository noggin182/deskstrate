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
