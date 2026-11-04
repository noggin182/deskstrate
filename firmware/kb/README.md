# kb: the ESP32 in the keyboard

An ESP32-S3 Mini (S3-Zero layout) inside the Keychron Q3 Max, wired to the keyboard's
STM32 (which runs `firmware/keyboard`). Hostname `deskstrate-kb`.

- **Wi-Fi** as `deskstrate-kb`, same network and credentials mechanism as the core and
  deck (`components/wifi_sta`).
- **Web page** at <http://deskstrate-kb.local/>: version, Wi-Fi signal, the keyboard
  link and the last lines the keyboard sent. `GET /api/status` is the same as JSON;
  `POST /api/kb` sends its body to the keyboard as one line.
- **UART to the keyboard**, newline-terminated text both ways. Lines from the
  keyboard are logged and shown; nothing is decoded yet. It sends
  `hello deskstrate-kb <version>` whenever the keyboard comes onto USB.
- **Deep sleep** once the keyboard has been off USB for 30 s, with the status LED off.
  It wakes when the keyboard is back on USB (a wake is a fresh boot).
- **Updates** from GitHub releases like the core and deck (`components/ota_pull`):
  2 minutes after each boot, hourly while awake, or on a doorbell (UDP 18212).
  Asleep it hears nothing, so it catches up on the next wake.
- **Status LED**: the WS2812 on GPIO21, same colours as the other boards
  (README.md, Status LED). Off while asleep.

## Wiring

| ESP32-S3 Mini | Keyboard | |
| --- | --- | --- |
| GPIO1 (RX) | STM32 PB12, LQFP pin 33 | keyboard → ESP; 1 kΩ in series |
| GPIO2 (TX) | STM32 PA3, LQFP pin 17 (USART2 RX) | ESP → keyboard; 1 kΩ in series |
| 5V | TP4056 pin 5 (BAT), or the battery lead | through the Mini's own regulator |
| GND | GND | must be shared with the keyboard |

The keyboard holds PB12 high (UART idle) while it has USB power and low without it.
That level is how this board knows the keyboard is on USB, so RX must be an RTC GPIO
(0-21) and has a pull-down: a disconnected line reads as "off USB".

Until the keyboard firmware has the UART side, note the pin-test build drives **both**
PB12 and PA3 as outputs. Don't connect GPIO2 to PA3 without the 1 kΩ resistor, or the
two outputs fight. With the pin test, RX simply reads high (awake) or low (asleep after
30 s) as Esc flips PB12.

Pins, baud (115200), and sleep are in menuconfig, Deskstrate keyboard. Turn sleep off
for bench work with nothing on RX.

## First flash

Identify the board by MAC first (see CLAUDE.md), then build once with the Wi-Fi
credentials set (menuconfig, Deskstrate Wi-Fi; they stay in the gitignored
`sdkconfig.s3_mini` and are stored in NVS):

```
pio run -d firmware/kb -t menuconfig
pio run -d firmware/kb -t upload --upload-port COMx
```

After that it updates itself from releases and never needs USB again, unless its NVS
is erased.
