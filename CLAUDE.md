# Deskstrate: notes for Claude

## Boards on the bench PC

Several ESP32-S3 boards are plugged in at once and Windows reassigns COM numbers
when they are replugged. Identify a board by MAC before flashing it, never by
COM number alone.

| Board | Firmware | MAC | Last seen on |
| --- | --- | --- | --- |
| Core (S3 Mini, `deskstrate`, 192.168.4.44) | `firmware/core` | `ac:27:6e:cb:3f:54` | COM15 |
| Deck (Feather ESP32-S3 TFT) | `firmware/deck` | `f4:12:fa:59:79:bc` | COM8 |
| Bench S3 Mini (hub SWD on GPIO1/GPIO2) | `firmware/swd_dump` | `ac:27:6e:cb:2c:d8` | COM16 |

- Find the ports: `Get-CimInstance Win32_PnPEntity | ? Name -match 'COM\d+'`.
- Read a MAC: `esptool.py --port COMx --after watchdog_reset read_mac`. A board on
  Wi-Fi also shows up in `arp -a`.
- Pass the port explicitly when uploading:
  `pio run -d firmware/<project> -t upload --upload-port COMx`.
- Wi-Fi credentials (core and deck) and the core's OTA token live in each board's NVS.
  A build with them blank in its gitignored `sdkconfig.*` (as on a fresh PC, or in CI)
  keeps using the stored ones, so it is safe to flash a board that has run before. A
  brand-new board, or one whose NVS was erased, needs one build with them set; the user
  enters those themselves via `-t menuconfig` (Deskstrate Wi-Fi / Deskstrate core).
- Both boards normally update themselves from GitHub releases of `main` (see
  Deployment in README.md). To push a bench build to the core over Wi-Fi instead: `POST /api/ota` with
  `Authorization: Bearer <CONFIG_DESKSTRATE_OTA_TOKEN from sdkconfig.s3_mini>`. Never
  print the token or the Wi-Fi credentials.

## ESP32-S3 USB Serial/JTAG quirks

- `--after hard_reset` (the default) and `--after no_reset` leave the S3 in its ROM
  bootloader: no app output, and the device is offline until it is reset or flashed.
  Use `--after watchdog_reset` (`swd_dump` sets `board_upload.after_reset`).
- With a watchdog reset the port drops, so esptool/pio reports a serial error or
  "FAILED" after a successful write. Check that the write completed rather than
  trusting the exit code.
- PlatformIO's Python (`~/.platformio/penv/Scripts/python.exe`) has pyserial. The
  system `python` is only the Microsoft Store stub.
