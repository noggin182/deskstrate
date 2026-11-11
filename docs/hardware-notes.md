# Hardware notes

Facts tested on the real desk, lessons learned the hard way, and options that were
considered and shelved (with why), so they aren't reopened without a new reason. Pins,
wiring and protocols live with their code (`firmware/*/README.md`, `kvm_proto.h`,
`kb_link_proto.h`); this file is for what isn't code.

## Desk

- Two **Dell P2723DE** monitors shared by a Windows PC (DisplayPort), a MacBook Pro M4
  Pro (USB-C, through a Belkin Thunderbolt dock with a separate USB-C to each Dell) and
  occasional HDMI guests.
- Two USB switches (Amazon B0F5WJBNRB, huasion/eKL 4-in-4-out): **HID** (hub 1,
  keyboard and mouse, follows the host being worked on) and **media** (hub 2, webcam
  and headset, stays put so a call survives a switch).
- Keyboard: Keychron Q3 Max UK ISO, with a XIAO ESP32S3 Plus inside (`firmware/kb`,
  `firmware/keyboard`).
- Desk audio is an off-the-shelf powered mixer (AU-401), outside Deskstrate.

## Monitors (Dell P2723DE)

- DDC/CI at I2C **0x37**, EDID at **0x50**. Commands are accepted on an inactive input
  and in standby.
- **VCP 0x60 (input):** DP = 0x0F (PC), HDMI = 0x11 (guest), USB-C = 0x1B (Mac). The
  read is 16-bit: high byte = the port the query came in on, low byte = the active
  input. Always mask with `& 0xFF`.
- **VCP 0xD6 (power):** 1 = on, 4 = standby. Never send 5 (soft off).
- **Standby is only accepted from the port the monitor is showing.** The core's DDC
  comes in over HDMI, so it can only put a monitor into standby while it shows HDMI.
  The PC and Mac daemons do it for their own inputs.
- After an input switch, reads return garbage for about 1 s; allow 2 s to settle.
- Input selection must be **manual**: with auto-select a monitor jumps to another live
  input when its host sleeps.
- EDID serials: **810562882 = left**, **810959170 = right**. Identify monitors by serial,
  never by bus or list order.
- DDC through the HDMI tap works with nothing on HDMI pin 18 (+5 V): the core has read
  both monitors through bare taps since 2026-10-26.
- Scaler is MStar (VCP C8 low byte 0x05). On DDC, 0x49 is its ISP/flash port and 0x59
  its debug port: never write to either. A backup of the Dells' firmware is kept on
  Matt's PC, deliberately not in this repo.
- If both Dells say "no USB signal" on the Mac input while the Mac still sees them
  (seen once after a power blip), power-cycle the Belkin dock, then the monitors' mains.
  Deskstrate isn't involved.

## Hosts

- **macOS:** switching a monitor's input away doesn't disconnect it. Mirroring with
  `CGConfigureDisplayMirrorOfDisplay` fails with CGError 1014 while the displays are
  asleep (wake them first). DDC still works while mirrored. displayplacer 1.4.0
  misreports `enabled:false` on macOS 26.
- **Windows:** dxva2 `SetVCPFeature` / `GetVCPFeatureAndVCPFeatureReply` work, also while
  the displays are asleep and in Duplicate mode (one HMONITOR, DDC still per monitor).
- **Mac on the work VPN** (Cisco Secure Client, LAN access locked off): it receives the
  core's broadcasts but can't send to the LAN. Anything the Mac must hear goes out as a
  LAN broadcast; nothing may depend on the Mac sending.

## USB switches

- Each has a **PY32F002A** running its factory firmware, controlled over SWD without
  reflashing. The current switches' PY32 runs at **3.3 V**, so SWD is wired straight to
  the core's GPIOs: no level shifter.
- Status word ("mailbox") at **0x200001FC** (`hub_swd::kStatusAddr`): byte 0 held,
  byte 1 requested host (0xFF none), byte 2 current, byte 3 applied; host indices are
  stored reversed (`3 - index`). Switching = read-modify-write byte 1. (The older
  switches had it at 0x200001F4; their dump is in `reference/oem`.)
- Identify a chip over SWD: DPIDR, then DBGMCU_IDCODE at 0x40015800 (PY32F002A/F003/F030
  = 0x60001000), UID at 0x1FFF0E00 (3 words), flash size at 0x1FFF0FFC.
- **No series resistor on SWCLK:** 200 Ω there gave IDCODE ACK 5 (out of step); 200 Ω on
  SWDIO alone is fine.
- **Port 1 is 12 Mb/s** on both switches: it goes through an internal full-speed hub
  shared with the onboard sound card, and has no SuperSpeed. Put only keyboards, mice and
  receivers on port 1; the webcam goes on ports 2 to 4. The switches are otherwise USB 3
  (5 Gb/s), shown on a Mac as a "USB3.1 Hub" plus a Genesys "USB2.1 Hub".

## Lessons learned

- **SWD ACK values:** 1 OK, 2 WAIT, 4 FAULT, 7 no reply (line pulled up); anything else
  (e.g. 5) means out of step or swapped lines.
- **Toggle-test every line first:** drive each GPIO low and high and check the right
  contact moves at the far end. Swapped SWCLK/SWDIO once cost hours. Convention on USB-C
  breakouts: D+ = SWDIO, D− = SWCLK.
- Many cheap USB-C breakouts wire only one D+/D− pair, so plug orientation matters;
  bridge A6↔B6 and A7↔B7.
- **TXB0104** works over short wires but fails over a 1 m cable with a pull-up (auto-
  direction sensing and one-shot edge boosts). Never use a TXB for I2C/DDC.
- **Series resistors** go at the driver's output where the line enters a cable.
- "5 V logic accepts 3.3 V" is only true for TTL-input parts.
- Twisted legs are not a joint: a loose resistor once looked exactly like a protocol bug.
- ESP32-S3 GPIOs are not 5 V tolerant: DDC goes through a BSS138 shifter (see
  `firmware/core/README.md`), never powered from HDMI pin 18.

## Shelved (and why)

| Option | Why not |
| --- | --- |
| SWD over 1 m USB-C cables from the desk unit | Unreliable; the core now sits next to the switches on short wires |
| Custom "link board" (USB-C, 74LVC1T45 buffers, ESD, polyfuse) | Short wires made it unnecessary |
| Power over CC1/CC2 to remote boxes | No longer needed |
| UART over D+/D− to a proxy MCU | Superseded by the central core |
| Proxying or isolating guest DDC through the HDMI tap | Tap-only works; not needed |
| Stacked dual HDMI PCB (Foxconn QJ11191-DFB2-4H, ~£43 for 5) | The panel-mount breakout taps work |
| Pi Zero W or Arduino Uno R4 WiFi as the core | ESP32-S3 chosen for shared code with the deck and OTA |
| Ethernet core (T-ETH-Lite, ESP32-POE-ISO) | Wi-Fi is fine where the core sits |
| HMAC-authenticated binary protocol | Chose a LAN-only, unauthenticated short ASCII protocol (`kvm_proto.h`) |
| Harvesting the switches' USB audio chips for desk audio | A switched hub attaches to one host only; bought a mixer instead |
