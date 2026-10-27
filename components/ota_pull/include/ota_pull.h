// Pull updates from GitHub Releases, shared by core and deck. A board only ever installs
// an image it fetched itself from the fixed release URL below, over certificate-checked
// HTTPS. Nothing is pushed into it from outside.
//
// CI publishes each build of main as a release with a manifest and one image per board:
//   <kReleases>/latest/download/manifest.txt
//     tag=build-42
//     deskstrate_core=<app version>
//     deskstrate_deck=<app version>
//   <kReleases>/download/<tag>/<project name>.bin
// A check reads the manifest. If the version listed for this board's project (the
// project() name in its CMakeLists) differs from the running one, the board downloads
// its image, refuses it unless the image's project name matches its own, writes it to
// the spare OTA slot and reboots into it. A version that rolled back is never retried.
//
// Checks happen kFirstPollMs after boot, then every kPollMs, and on a doorbell: a UDP
// datagram to the board's port whose first bytes are kDoorbellMagic ("DSUP"). Nothing
// else in it is read or trusted. Checks are at most one per kMinGapMs, so flooding the
// port costs one outbound GET a minute.
//
// A local build ("-dirty" version) is replaced by a doorbell but never by the poll, so
// a bench build isn't overwritten behind your back.
//
// Shows on the status LED (status_led.h): purple from a doorbell until its check is done,
// flashing purple while an image is written, green for the reboot into it, red while the
// last check failed.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace ota_pull {

inline constexpr char kReleases[] = "https://github.com/noggin182/deskstrate/releases";
inline constexpr uint8_t kDoorbellMagic[4] = {'D', 'S', 'U', 'P'};
inline constexpr uint16_t kCorePort = 18210;  // doorbell port, UDP
inline constexpr uint16_t kDeckPort = 18211;

inline constexpr uint32_t kFirstPollMs = 2 * 60 * 1000;
inline constexpr uint32_t kPollMs = 60 * 60 * 1000;
inline constexpr uint32_t kMinGapMs = 60 * 1000;
inline constexpr uint32_t kConfirmTimeoutMs = 60 * 1000;

// Starts the poll/doorbell task. Call once Wi-Fi has been started (wifi_sta / deck
// network), from either board.
esp_err_t start(uint16_t doorbell_port);

// Rollback guard, call once after start-up. A new image boots pending verify: this marks
// it valid once Wi-Fi has an IP within kConfirmTimeoutMs (and `healthy`, the app's own
// services started, is true); otherwise it rolls back to the previous image and reboots.
// No-op for an image that isn't pending verify (e.g. flashed over USB).
void confirm_boot(bool healthy);

// The last check as a JSON object: {"result":"...","release":"build-42","ago_s":N}
// (ago_s -1 before the first check). Returns the length written.
int status_json(char *buf, size_t len);

}  // namespace ota_pull
