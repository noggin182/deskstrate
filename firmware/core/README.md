# Deskstrate core

The core is the hidden central box and the source of truth. It runs headless,
switches both monitors over DDC/CI and both USB hubs over SWD, and serves the
deck and host daemons over UDP (`kvm_proto`, port 18200). Hostname:
`deskstrate-core`.

Status: skeleton. It joins Wi-Fi and runs a read-only DDC1 bring-up logger.
The UDP server, switching logic, SWD and OTA follow.

## Build

Board: diymore ESP32-S3 Mini (S3-Zero layout), env `s3_mini`. Chip is an
ESP32-S3FH4R2 (4MB flash, 2MB PSRAM); the partition table is in `partitions.csv`.

```
pio run -d firmware/core -t upload
pio run -d firmware/core -t menuconfig   # Deskstrate Wi-Fi / Deskstrate core
```

Wi-Fi credentials live in `sdkconfig.s3_mini`. Don't commit it.

## Pins

Defined in `src/board_config.h`. Still to be checked against the silkscreen and
toggle-tested.

| Function | GPIO |
| --- | --- |
| DDC1 SDA / SCL | GPIO1 / GPIO2 |
| DDC2 SDA / SCL | GPIO4 / GPIO5 |
| HID hub SWCLK / SWDIO | GPIO6 / GPIO7 |
| Media hub SWCLK / SWDIO | GPIO8 / GPIO9 |

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
