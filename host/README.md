# Host daemons

A small program on each host that follows the core's `STATE` broadcasts
(`components/kvm_proto`, UDP 18200) and does what only the host can do:

| Situation (from `STATE`) | What the host does |
| --- | --- |
| Both monitors on my input | Extend |
| One monitor on my input | Mirror onto it (Windows: Duplicate) |
| No monitor on my input | Leave the layout alone |
| A monitor comes (back) to my input, awake | Wake my displays |
| The keyboard (HID hub) moves to me | Wake my displays |
| The desk is asleep (standby in `STATE`) | DDC standby (VCP D6 = 4) on the monitors showing my input |
| The desk is awake | DDC on (D6 = 1) on the monitors showing my input |

A monitor in standby on my input keeps its place in the layout. The desk's sleep is
held by the core (the keyboard's Sleep key or the deck's menu asks for it, and any other
request or key press wakes it), but only the host can do the DDC for its own input: the
core's DDC comes in over HDMI, which a Dell ignores for standby unless HDMI is showing.
The power rule is followed on every `STATE` (at most every third, about 6 s, while
nothing changes), not only when it changes. DDC only: the signal stays on, so there's no
"No signal" and the OS doesn't see a display sleep (which would lock the Mac). It assumes
the hosts never turn their displays off themselves: a monitor without a signal would be
switched back on to show "No signal".

The rules live in one place, `common/host_policy.h` (C++, used by both
daemons), with tests in `common/test_host_policy.cpp`:

```
c++ -std=c++17 -I components/kvm_proto/include -I host/common host/common/test_host_policy.cpp && ./a.out
```

The daemons only listen. The one thing they send is an empty `REQUEST`
(`KR----`), broadcast until a `STATE` comes back so they find the core
without waiting for its heartbeat; `--core <ipv4>` sends it to that address
instead.

## Windows (PC)

One exe, `deskstrate-host.exe`, normally installed as a service:

- **Service** (`--service`, LocalSystem, starts at boot): owns UDP 18200 and the
  protocol, and gets suspend, shutdown and display on/off from the service
  manager, so the core hears about them even before anyone signs in.
- **Agent** (`--agent`): session 0 can't touch the displays, so the service
  starts its own exe as SYSTEM in the console session and passes it each display
  action (sleep, wake, extend, duplicate) over a pipe. The agent acts on
  whichever desktop is showing: the sign-in screen, the lock screen or the
  user's desktop. It's started on first use and again after it exits or another
  user takes the console.
- **Standalone** (no option): everything in one process with a tray icon, in
  the user's session only. For debugging, or a no-admin install; it can't act
  at the sign-in or lock screen.

Build (Visual Studio 2022 with the C++ workload, or take the
`deskstrate-host-windows` artifact from the "Host daemons" workflow run):

```
cmake -S host/windows -B host/windows/build -A x64
cmake --build host/windows/build --config Release
```

Install the service from an elevated PowerShell. It copies the exe to
`%ProgramFiles%\Deskstrate`, registers the service (restart on failure) and adds
a firewall rule for UDP 18200 from the local subnet:

```
powershell -ExecutionPolicy Bypass -File host\windows\install.ps1 -Exe host\windows\build\Release\deskstrate-host.exe
```

`-User` installs the standalone tray app instead (Startup shortcut, no admin;
allow the firewall prompt on Private networks). `-Uninstall` removes either.

The keyboard's Fn+knob (while the keyboard is on the Mac) presses the PC's volume
keys, with Windows' usual on-screen indicator. DDC goes through dxva2, in the agent.

Logs: `%ProgramData%\Deskstrate\service.log` and `agent.log` (service), or
`%LOCALAPPDATA%\Deskstrate\host.log` (standalone). Options `--no-topology` and
`--core <ipv4>` go after `--service` in the service's command line.

## macOS (Mac)

A LaunchAgent (`com.deskstrate.host`) in the user's session, restarted if it
exits. Needs the Xcode command line tools (`xcode-select --install`).

```
make -C host/mac cert        # once: a signing certificate, so rebuilds keep Accessibility
make -C host/mac install     # build, install to ~/Library/Application Support/Deskstrate, start
./mac-install.sh             # from the repo root: git pull, then make install
make -C host/mac uninstall
host/mac/build/deskstrate-host --list-displays   # serials, origins, mirroring
```

Log: `~/Library/Logs/Deskstrate/host.log`. The Dells are found by EDID serial
(810562882 left, 810959170 right); if `--list-displays` shows other serials,
pass `--left-serial` / `--right-serial` (add them to `ProgramArguments` in
`com.deskstrate.host.plist`). macOS may ask to allow it to find devices on the
local network: allow it, or it never hears the core. (The binary carries an
Info.plist, `host/mac/Info.plist`, so macOS can ask; if the log shows no `STATE` lines
while running it by hand from Terminal does, check System Settings > Privacy & Security >
Local Network.)

The keyboard's Fn+knob (while the keyboard is on the PC) changes the Mac's volume,
and it follows the desk's sleep over DDC (above). `deskstrate-host --ddc-list` shows each external display's input and
power over DDC; `--ddc standby` / `--ddc on` try it by hand. DDC on Apple Silicon uses the
private IOAVService calls, as MonitorControl does.
To get macOS's own volume steps and HUD, add `deskstrate-host` under System
Settings > Privacy & Security > Accessibility; without that it sets the output's
volume directly in 1/16 steps, with no HUD. An output with no software volume (some
USB DACs: the volume keys are greyed out) can't be changed either way.

macOS ties that permission to the binary's signature. `make cert` (once; it asks for your
password to trust the certificate) creates a self-signed "Deskstrate Host Signing"
certificate in the login keychain, and every build is then signed with it, so the
permission survives rebuilds. Without it the build is ad-hoc signed, looks like a new app
each time, and needs removing (−) and re-adding in Accessibility after every
`make install`; switching the old entry off and on isn't enough.

Building it on the Mac (rather than downloading the CI artifact) avoids
Gatekeeper quarantine on an unsigned binary.
