# bridge: the Mac's serial bridge

An ESP32-S3 Super Mini (HW-747) plugged into the Mac's dock. The Mac is on a work VPN
that lets LAN broadcasts in but nothing out, so its daemon (`host/mac`) never touches
the network: it talks `kvm_proto` over this board's USB serial port instead, and the
board passes it to and from UDP 18200 on Wi-Fi. Hostname `deskstrate-bridge`.

It is a plain passthrough and never reads `kvm_proto`, so protocol changes never need a
bridge update.

- **Serial to UDP:** each line from the Mac goes out as one broadcast to
  255.255.255.255:18200, without its line ending. A line ends at `\r`, `\n` or `\r\n`.
  Empty lines are skipped; lines over 256 bytes are dropped.
- **UDP to serial:** every datagram heard on 18200 goes to the Mac as one line ending in
  `\n`: broadcasts, and the core's direct replies to the bridge. Its own broadcasts
  coming back, datagrams over 256 bytes and datagrams containing `\r` or `\n` are dropped.
- **Port:** the S3's built-in USB Serial/JTAG. The Mac sees `/dev/cu.usbmodem…` with
  USB ID 303A:1001 and the board's MAC as the serial number, with no driver. The daemon
  only opens the bridge with the serial number it was installed with.
- **Logs:** ESP_LOG stays on the same port, with every line starting `# `, so it reads
  as a comment; the daemon copies those lines into its host.log. One lock guards every
  write, so a log never lands inside a data line. The ROM prints a few unprefixed lines
  at power-up (the bootloader only warnings); the daemon ignores anything that isn't
  `# ` or `kvm_proto`.
- **Mac not listening:** writes are skipped while no USB host is attached and give up
  after 20 ms, so an undocked, asleep or not-reading Mac never stalls the bridge.
- **Wi-Fi power save is off** (it's on USB power), so broadcasts aren't held for the
  router's DTIM beacon.
- **Updates** from GitHub releases like the other boards (`components/ota_pull`):
  2 minutes after boot, hourly, or on a doorbell (UDP 18213).
- **Status LED:** the Super Mini's WS2812 on GPIO48, with the usual colours (README.md,
  Status LED). menuconfig, Deskstrate bridge, for another board.

## Bring-up

1. Flash it once over USB (find its port by MAC first, see CLAUDE.md):
   `pio run -d firmware/bridge -t upload --upload-port COMx`. The spare Super Mini ran
   the kb firmware before, so its Wi-Fi is already in NVS. A fresh board needs Wi-Fi set
   once in `-t menuconfig`, Deskstrate Wi-Fi.
2. Plug it into the Mac's dock (not a switched hub port) and run `./mac-install.sh`. With
   the bridge the only ESP plugged into the Mac, the install pins the daemon to it.
3. host.log should show `docked: bridge …`, then `STATE` lines and `volume 42%`; the
   deck's Mac row shows the Mac's volume.
4. Undock: host.log shows `undocked: …` and then stays quiet, and the deck's Mac row
   goes empty within 30 s.

On a monitor it looks like this (`pio device monitor -d firmware/bridge`):

```
# I (512) bridge: Deskstrate bridge build-260.1
# I (1630) udp: passing UDP 18200 to and from the Mac
KS PPMM 42% ---
KS PPMM 42% ---
```

and typing `KR ---- --- ---` gets a `KS` straight back.
