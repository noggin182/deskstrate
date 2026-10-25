# Deskstrate: notes for Claude

## Boards on the bench PC

Several ESP32-S3 boards are plugged in at once and Windows reassigns COM numbers
when they are replugged. Identify a board by MAC before flashing it, never by
COM number alone.

| Board | Firmware | MAC | Last seen on |
| --- | --- | --- | --- |
| Core (S3 Mini, `deskstrate-core`, 192.168.4.33) | `firmware/core` | `ac:27:6e:cb:3f:54` | COM11 |
| Deck (Feather ESP32-S3 TFT) | `firmware/deck` | `f4:12:fa:59:79:bc` | COM8 |
| Bench S3 Mini (hub SWD on GPIO1/GPIO2) | `firmware/swd_dump` | `ac:27:6e:cb:2c:d8` | COM10 |

- Find the ports: `Get-CimInstance Win32_PnPEntity | ? Name -match 'COM\d+'`.
- Read a MAC: `esptool.py --port COMx --after watchdog_reset read_mac`. A board on
  Wi-Fi also shows up in `arp -a`.
- Pass the port explicitly when uploading:
  `pio run -d firmware/<project> -t upload --upload-port COMx`.
- Before flashing core or deck, check that `CONFIG_DESKSTRATE_WIFI_SSID` is set in that
  project's gitignored `sdkconfig.*`. Credentials only live there, so a copy can be
  empty on a fresh PC, and flashing it takes the board off the network. The user enters
  credentials themselves via `-t menuconfig` (Deskstrate Wi-Fi).

## ESP32-S3 USB Serial/JTAG quirks

- `--after hard_reset` (the default) and `--after no_reset` leave the S3 in its ROM
  bootloader: no app output, and the device is offline until it is reset or flashed.
  Use `--after watchdog_reset` (`swd_dump` sets `board_upload.after_reset`).
- With a watchdog reset the port drops, so esptool/pio reports a serial error or
  "FAILED" after a successful write. Check that the write completed rather than
  trusting the exit code.
- PlatformIO's Python (`~/.platformio/penv/Scripts/python.exe`) has pyserial. The
  system `python` is only the Microsoft Store stub.
