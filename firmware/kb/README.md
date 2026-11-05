# kb: the ESP32 in the keyboard

A Seeed XIAO ESP32S3 Plus (16MB flash, external antenna on u.FL) inside the Keychron
Q3 Max, wired to the keyboard's STM32 (which runs `firmware/keyboard`). Hostname `deskstrate-kb`.

- **Wi-Fi** as `deskstrate-kb`, same network and credentials mechanism as the core and
  deck (`components/wifi_sta`).
- **Web page** at <http://deskstrate-kb.local/>: version, Wi-Fi signal, the keyboard's
  status (mode, host OS, layer, battery) and link counters, and the last text it sent.
  `GET /api/status` is the same as JSON; `POST /api/kb` sends its body (up to 32
  characters) to the keyboard, which echoes it back, so the page tests the link both ways.
- **Link to the keyboard**: framed UART at 57600 with a CRC and acks
  (`components/kb_link_proto`, shared with `firmware/keyboard`). A KVM key on the
  keyboard arrives as a request, is acked, and goes to the core as a `kvm_proto`
  REQUEST, resent until the core's STATE shows it (`kvm_client`). Each new STATE goes
  back to the keyboard.
- **Deep sleep** once the keyboard has been off USB for 30 s, with the status LED off.
  It wakes when the keyboard is back on USB (a wake is a fresh boot).
- **Updates** from GitHub releases like the core and deck (`components/ota_pull`):
  2 minutes after each boot, hourly while awake, or on a doorbell (UDP 18212).
  Asleep it hears nothing, so it catches up on the next wake.
- **Status LED**: the XIAO's yellow user LED (GPIO21, lit when low), showing the
  other boards' colours as blink patterns (README.md, Status LED). Off while asleep.
  An S3 Mini's WS2812 works too: menuconfig, Deskstrate keyboard, Status LED kind.

## Wiring

| XIAO ESP32S3 | Keyboard | |
| --- | --- | --- |
| D0 / GPIO1 (RX) | STM32 PB12, LQFP pin 33 | keyboard → ESP; 1 kΩ in series |
| D1 / GPIO2 (TX) | STM32 PA3, LQFP pin 17 (USART2 RX) | ESP → keyboard; 1 kΩ in series |
| 5V | TP4056 pin 5 (BAT), or the battery lead | through the XIAO's own regulator |
| GND | GND | must be shared with the keyboard |

The keyboard holds PB12 high (UART idle) in cable mode with USB power and low otherwise.
That level is how this board knows the keyboard is on USB, so RX must be an RTC GPIO
(0-21) and has a pull-down: a disconnected line reads as "off USB".

The keyboard holds PB12 high only in cable mode with USB power, so the ESP sleeps on
Bluetooth and 2.4 GHz too. Keep the 1 kΩ resistors: the XIAO runs from the battery but
the STM32 from the keyboard's 3.3 V rail, which is off with the switch at Off, and the
resistor limits what the XIAO's TX can push into an unpowered PA3.

Pins and sleep are in menuconfig, Deskstrate keyboard. Turn sleep off
for bench work with nothing on RX. To wake it on the bench, jumper D0 to 3V3.

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
