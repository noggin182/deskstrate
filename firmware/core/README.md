# Deskstrate core

The core is the hidden central box and the source of truth. It runs headless,
switches both monitors over DDC/CI and both USB hubs over SWD, and serves the
deck and host daemons over UDP (`kvm_proto`, port 18200). Hostname:
`deskstrate-core`.

Status: skeleton. It joins Wi-Fi and runs a read-only DDC1 bring-up logger.
The UDP server, switching logic, SWD and OTA follow.

## Build

| Env | Board |
| --- | --- |
| `feather_bench` (default) | Adafruit Feather ESP32-S3 TFT, standing in on the bench |
| `s3_mini` | diymore ESP32-S3 Mini (S3-Zero layout), unconfirmed |

```
pio run -d firmware/core -e feather_bench -t upload
pio run -d firmware/core -e feather_bench -t menuconfig   # Deskstrate Wi-Fi / Deskstrate core
```

Each env keeps its own `sdkconfig.<env>`, so set the Wi-Fi credentials once per
env. Don't commit those files.

## Pins

Defined per env in `src/board_config.h`.

| Function | Feather bench | S3 Mini |
| --- | --- | --- |
| DDC1 SDA / SCL | GPIO1 (TX) / GPIO2 (RX) | GPIO1 / GPIO2 |
| DDC2 SDA / SCL | GPIO8 (A5) / GPIO14 (A4) | GPIO4 / GPIO5 |
| HID hub SWCLK / SWDIO | GPIO17 (A1) / GPIO18 (A0) | GPIO6 / GPIO7 |
| Media hub SWCLK / SWDIO | GPIO15 (A3) / GPIO16 (A2) | GPIO8 / GPIO9 |

DDC, per line: GPIO -> BSS138 LV, HV -> 4.7 k to 5 V -> 100 R -> HDMI pin 16
(SDA) / 15 (SCL); GND -> pin 17. Never power the shifter from HDMI pin 18.
Keep the two DDC buses separate (I2C_NUM_0 and I2C_NUM_1). ESP32-S3 GPIOs are
not 5 V tolerant.

## DDC1 bring-up

With `DESKSTRATE_DDC_BRINGUP` (default on), DDC1 is logged every 5 s:

- ACKs on 0x37 and 0x50
- VCP 0x60, raw and `& 0xFF`
- VCP 0xD6
- the EDID serial

It never writes. `DESKSTRATE_DDC_TOGGLE_TEST` drives SDA then SCL low/high for
3 s each at boot, so you can follow them with a meter before I2C starts.
