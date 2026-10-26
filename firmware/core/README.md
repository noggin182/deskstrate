# Deskstrate core

The core is the hidden central box and the source of truth. It runs headless,
switches both monitors over DDC/CI and both USB hubs over SWD, and serves the
deck and host daemons over UDP (`kvm_proto`, port 18200). Hostname:
`deskstrate`.

Status: joins Wi-Fi, polls both monitors (DDC/CI) and both hubs (SWD), serves a
bench web UI at `http://deskstrate.local/`, runs the `kvm_proto` UDP server and
takes updates over Wi-Fi. Presets/switching logic and protocol authentication
follow.

## Missing hardware

Any of the two monitors and two hubs can be absent (not cabled, powered off, or
not wired yet). It shows as "not connected" in the web UI, as `KVM_IN_KEEP` /
`KVM_USB_UNKNOWN` in `STATE`, and is retried every 15 s, so it is picked up
when it appears without a reboot. Switching an absent device fails: HTTP 409
from the web API, `KVM_RES_REJECTED` over UDP.

Monitors are matched to left/right by EDID serial, re-read whenever a monitor
reappears. Until a serial has been read, bus order is assumed (DDC1 = left), but
a bus already identified as one monitor never stands in for the other.

A hub request while the hub is still carrying out the previous switch is refused
(`KVM_RES_BUSY`) rather than racing its firmware.

## UDP (kvm_proto)

The core binds `KVM_PORT` (18200) and broadcasts a `STATE` on every change and
every 2 s (`KVM_HEARTBEAT_MS`), with the monitors' inputs (left/right by EDID
serial, `KVM_IN_OFF` for standby) and both hubs' ports (0-3). It answers
`REQUEST`s with a `RESULT`; `KVM_ACT_MON` and `KVM_ACT_USB` are implemented.
Results: `OK`, `UNKNOWN` (bad action or argument), `REJECTED` (device not
connected or the write failed), `BUSY` (hub mid-switch).

Not yet implemented: HMAC tags (sent as zeros, not checked) and per-sender
counter checks, so treat it as bench-only on a trusted LAN.

## Build

Board: diymore ESP32-S3 Mini (S3-Zero layout), env `s3_mini`. Chip is an
ESP32-S3FH4R2 (4MB flash, 2MB PSRAM); the partition table is in `partitions.csv`
(two OTA slots of 0x1F0000, `ota_0` / `ota_1`).

```
pio run -d firmware/core -t upload
pio run -d firmware/core -t menuconfig   # Deskstrate Wi-Fi / Deskstrate core
```

Wi-Fi credentials and the OTA token are stored in the core's NVS. A build with
them set in `sdkconfig.s3_mini` (gitignored, never commit it) writes them there;
a build with them blank, like CI's, keeps the stored ones. So a new board needs
one USB flash of a build with both set, and nothing after that does.

CI (`.github/workflows/firmware.yml`) builds core and deck on every push and
keeps `firmware.bin`, `bootloader.bin` and `partitions.bin` as artifacts. Those
images carry no secrets.

## OTA

Build (or take `firmware.bin` from a CI run), then post the app image to the
core with the token (`CONFIG_DESKSTRATE_OTA_TOKEN`, menuconfig: Deskstrate core):

```
pio run -d firmware/core
curl -H "Authorization: Bearer $TOKEN" --data-binary @firmware/core/.pio/build/s3_mini/firmware.bin http://deskstrate.local/api/ota
```

It is written to the spare slot, checked (image header, chip, SHA-256) and the
core reboots into it. `GET /api/ota` shows the running slot, version (git
describe), build time, state and whether a token is set (`auth`).

Rollback is on: a new image boots in pending-verify and marks itself valid once
Wi-Fi has an IP and the web server is up. If it can't within 60 s, or crashes
first, the bootloader goes back to the previous image. Only app changes go over
the air; a new bootloader or partition table needs USB.

Uploads need the token: 401 without it, 403 if the core has none stored. The
rest of the web API is still unauthenticated, so keep the core LAN-only.

That is the push path, for the bench. Normally the core updates itself from
GitHub releases (doorbell on UDP 18210, or hourly; see Deployment in the top
README and `components/ota_pull`). `GET /api/ota` shows the last check under
`pull`. A local `-dirty` build is only replaced by a doorbell, never by the
hourly poll.

## Pins

Defined in `src/board_config.h`. VERIFIED on the wired core (2026-10-26); both
DDC buses confirmed by a monitor answering on them.

| Function | GPIO |
| --- | --- |
| DDC1 (monitor 1) SDA / SCL | GPIO11 / GPIO12 |
| DDC2 (monitor 2) SDA / SCL | GPIO10 / GPIO9 |
| HID hub (hub 1) SWCLK / SWDIO | GPIO1 / GPIO5 |
| Media hub (hub 2) SWCLK / SWDIO | GPIO6 / GPIO4 |

DDC, per line: GPIO -> BSS138 LV, HV -> 4.7 k to 5 V -> 100 R -> HDMI pin 16
(SDA) / 15 (SCL); GND -> pin 17. Never power the shifter from HDMI pin 18.
Keep the two DDC buses separate (I2C_NUM_0 and I2C_NUM_1). ESP32-S3 GPIOs are
not 5 V tolerant.

## Bring-up options

menuconfig, Deskstrate core:

- `DESKSTRATE_DDC_TOGGLE_TEST` drives DDC1 SDA then SCL low/high for 3 s each at
  boot, so you can follow them with a meter before I2C starts.
- `DESKSTRATE_SWD_BRINGUP` logs each hub's SW-DP IDCODE and selection state once
  at boot. Read-only.
