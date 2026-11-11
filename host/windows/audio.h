// The PC's own volume, for reporting to the core (host/common/level_report.h).
//
// Reads the default output device (render, console role) through the Core Audio API and
// posts a message to the protocol window whenever its level, its mute or the default
// device itself changes. Runs in the service (session 0) as well as standalone: endpoint
// volume is per device, not per session.
#pragma once

#include <windows.h>

namespace audio {

// Watches the default output device; posts `msg` to `hwnd` on any change. Call from the
// protocol thread, after CoInitializeEx. False if Core Audio isn't available.
bool start(HWND hwnd, UINT msg);

// Call from the protocol thread when `msg` arrives: re-reads (and, after a device change,
// re-binds). Level 0-100 and mute; false if there is no output device.
bool read(int &level, bool &muted);

void stop();

}  // namespace audio
