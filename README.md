# Deskstrate

Desk orchestrator: a DIY KVM for two Dell P2723DE monitors shared between a
Windows PC, a MacBook and HDMI guests. Monitor inputs switch over DDC/CI; two
USB hubs switch over SWD.

| Path | What |
| --- | --- |
| `firmware/core` | Central box (ESP32-S3 Mini). Headless source of truth: DDC, SWD, UDP server. `deskstrate-core` |
| `firmware/deck` | Desk unit (Feather ESP32-S3 TFT + NeoKeys). Screen and keys; client of the core. `deskstrate-deck` |
| `components/kvm_proto` | UDP wire format shared by core, deck and the host daemons |
| `components/wifi_sta` | Shared Wi-Fi station join (menuconfig: Deskstrate Wi-Fi) |
| `firmware/test` | Arduino DDC/CI probe for a 5 V ATmega32u4 |

Each firmware is its own PlatformIO project: build with
`pio run -d firmware/core` or `pio run -d firmware/deck`, or open
`deskstrate.code-workspace` in VS Code to get both.
