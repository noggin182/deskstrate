# Deskstrate

Desk orchestrator: a DIY KVM for two Dell P2723DE monitors shared between a
Windows PC, a MacBook and HDMI guests. Monitor inputs switch over DDC/CI; two
USB hubs switch over SWD.

| Path | What |
| --- | --- |
| `firmware/core` | Central box (ESP32-S3 Mini). Headless source of truth: DDC, SWD, UDP server. `deskstrate` |
| `firmware/deck` | Desk unit (Feather ESP32-S3 TFT + NeoKeys). Screen and keys; client of the core. `deskstrate-deck` |
| `components/kvm_proto` | UDP wire format shared by core, deck and the host daemons |
| `components/wifi_sta` | Shared Wi-Fi station join (menuconfig: Deskstrate Wi-Fi) |
| `components/ota_pull` | Shared self-update from GitHub Releases: hourly poll or UDP doorbell, with rollback |
| `tools` | `release_manifest.py` (CI) and `doorbell.py` (ring the boards' update doorbells) |
| `firmware/test` | Arduino DDC/CI probe for a 5 V ATmega32u4 |

Each firmware is its own PlatformIO project: build with
`pio run -d firmware/core` or `pio run -d firmware/deck`, or open
`deskstrate.code-workspace` in VS Code to get both.

## Deployment

Every push to `main` is built by GitHub Actions (`.github/workflows/firmware.yml`)
and published as a release (`build-<run>.<attempt>`) with `manifest.txt`,
`deskstrate_core.bin` and `deskstrate_deck.bin`. CI then rings each board's
doorbell, and the boards download and install their own image from the release
over HTTPS. Nothing is pushed into the house: the doorbell is a UDP datagram
starting with `DSUP` that only means "check now", and the boards only ever fetch
from this repo's releases. They also poll hourly, so a missed ring only delays
an update. A version that rolls back is never retried.

| Board | Doorbell (UDP) |
| --- | --- |
| core | 18210 |
| deck | 18211 |

The router forwards those two UDP ports from `deskstrate.nogsnet.com` (A record,
IPv4) to the core and deck. Forward nothing else: the core's web UI (80) and
`kvm_proto` (18200) are unauthenticated. IPv4 because GitHub's hosted runners
have no IPv6, so they can't reach an IPv6 pinhole. Set the repo variable
`DOORBELL_HOST` to ring a different host.

Ring by hand with `python tools/doorbell.py <host> 18210 18211` (on the LAN, use
each board's own address). Checks are rate-limited to one a minute per board.
Anyone who can push to `main` can deploy, so protect the account (2FA) and the
branch.
