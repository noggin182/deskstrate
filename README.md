# Deskstrate

Desk orchestrator: a DIY KVM for two Dell P2723DE monitors shared between a
Windows PC, a MacBook and HDMI guests. Monitor inputs switch over DDC/CI; two
USB hubs switch over SWD.

| Path | What |
| --- | --- |
| `firmware/core` | Central box (ESP32-S3 Mini). Headless source of truth: DDC, SWD, UDP server. `deskstrate` |
| `firmware/deck` | Desk unit (Feather ESP32-S3 TFT + NeoKeys). Screen and keys; client of the core. `deskstrate-deck` |
| `firmware/kb` | XIAO ESP32S3 Plus inside the keyboard: Wi-Fi and a UART to the keyboard's STM32, asleep off USB. `deskstrate-kb` |
| `firmware/keyboard` | QMK for the Keychron Q3 Max (UK ISO): our keymap and link code on top of Keychron's fork, plus the VIA definition |
| `host` | Host daemons for the PC (Windows service) and the Mac (LaunchAgent): display layout, wake on switch, the desk's sleep (DDC standby on their own input), and volume from the keyboard's Fn+knob |
| `components/kvm_proto` | UDP wire format shared by core, deck, kb and the host daemons |
| `components/kvm_controls` | What a deck key or keyboard Fn key asks the core for; shared by deck and kb, tested on a PC |
| `components/kb_link_proto` | Framed UART format between the keyboard's STM32 and the kb ESP |
| `components/wifi_sta` | Shared Wi-Fi station join (menuconfig: Deskstrate Wi-Fi) |
| `components/ota_pull` | Shared self-update from GitHub Releases: hourly poll or UDP doorbell, with rollback |
| `components/web_tls` | Shared HTTPS for the core's and kb's web UI: key made on the board, Let's Encrypt certificate pulled from the `tls` release (see `tls/README.md`) |
| `components/status_led` | Shared status LED (colours below) |
| `tls` | The boards' CSRs; `.github/workflows/tls.yml` turns them into certificates |
| `tools` | `release_manifest.py` (CI), `doorbell.py` (ring the boards' update doorbells), `render_icons.py` (the deck's logos) |
| `firmware/bench` | Signs of life for the bench ESP32-S3 Mini: rainbow LED and a hello-world page. No OTA, no sleep. `esp-bench` |
| `firmware/radio_test` | Bench tool: Wi-Fi signal test from inside the closed keyboard |
| `firmware/swd_dump` | Bench tool: read-only SWD flash dump of a hub's PY32 |
| `reference/oem` | The old hubs' factory firmware dump and its reverse engineering (Ghidra), for the SWD mailbox |
| `docs/hardware-notes.md` | Verified hardware facts, lessons learned, and options considered and shelved |

Each firmware is an ESP-IDF project (sources in `src/`). CI builds them with plain
ESP-IDF; `platformio.ini` is kept for flashing on the bench: `pio run -d firmware/core`,
`firmware/deck` or `firmware/kb`, or open `deskstrate.code-workspace` in VS Code to get all
three. PlatformIO's debugger works on each over the S3's built-in USB JTAG
(`debug_tool = esp-builtin`).

## Deployment

Every push to `main` that changes a board is built by GitHub Actions (`.github/workflows/firmware.yml`)
and published as a release (`build-<run>.<attempt>`) with `manifest.txt`,
`deskstrate_core.bin`, `deskstrate_deck.bin` and `deskstrate_kb.bin`. Every release carries all three
images, so CI keeps only the newest 10. CI then rings each board's
doorbell, and the boards download and install their own image from the release
over HTTPS. Nothing is pushed into the house: the doorbell is a UDP datagram
starting with `DSUP` that only means "check now", and the boards only ever fetch
from this repo's releases. They also poll hourly (5 minutes after a failed
check, which shows red on the status LED), so a missed ring only delays an
update. A version that rolls back is never retried.

A rebuilt board's firmware version is the release name (`build-59.1`, as shown at
`/api/ota`). A board whose sources didn't change keeps the image, and so the version,
of the release it already runs. Local and branch builds carry `git describe` instead
(e.g. `build-58.1-2-gf367e8d`, with `-dirty` for uncommitted changes).

| Board | Doorbell (UDP) |
| --- | --- |
| core | 18210 |
| deck | 18211 |
| kb | 18212 |

The router forwards those UDP ports from `deskstrate.nogsnet.com` (A record,
IPv4) to each board. Forward nothing else: the core's web UI (80) and
`kvm_proto` (18200) are unauthenticated. IPv4 because GitHub's hosted runners
have no IPv6, so they can't reach an IPv6 pinhole. Set the repo variable
`DOORBELL_HOST` to ring a different host.

Ring by hand with `python tools/doorbell.py <host> 18210 18211 18212` (on the LAN, use
each board's own address). Checks are rate-limited to one a minute per board.
Anyone who can push to `main` can deploy, so protect the account (2FA) and the
branch.

### Status LED

Each board's built-in WS2812 is its status LED (`components/status_led`): GPIO21
on the core's S3 Mini, the NeoPixel on the deck's Feather. The keyboard's XIAO
ESP32S3 has a plain LED (GPIO21, lit when low) instead, which shows the same states as
blink patterns: 5 Hz updating, 1 Hz booting or busy, on for a fault, a blip every 2 s
when all is well.

| Colour | Meaning |
| --- | --- |
| Blue, dim | All good |
| Green | Booting, joining Wi-Fi, or restarting into a new image |
| Purple | Doorbell rang: checking for an update |
| Purple, flashing | Writing an update to the spare slot |
| Red | A fault: a service didn't start, no Wi-Fi, or the last update check failed |
| Orange, bright | Deck only: no `STATE` from the core for 10 s |

On the deck, whichever colour is showing fades to an eighth of its brightness while the
desk is asleep, like the key lights.

## CI workflows

| Workflow | File | Runs when | Produces |
| --- | --- | --- | --- |
| Firmware: core, deck, kb (release + OTA) | `firmware.yml` | core, deck, kb or `components/` change | On `main`: a `build-<run>.<attempt>` release, then rings the doorbells |
| Keyboard (QMK) | `keyboard.yml` | `firmware/keyboard` or `kb_link_proto` change | On `main`: the `.bin` and VIA JSON on the `keyboard` prerelease |
| Host daemons: Windows, Mac | `host.yml` | `host/` or `kvm_proto` change | Policy tests; the daemons as run artifacts |
| TLS certificates (renew) | `tls.yml` | weekly, or a `tls/*.csr` changes on `main` | Certificates on the `tls` prerelease |

The bench tools (`firmware/bench`, `radio_test`, `swd_dump`) aren't built in CI.
