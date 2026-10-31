# Host daemons

A small program on each host that follows the core's `STATE` broadcasts
(`components/kvm_proto`, UDP 18200) and does what only the host can do:

| Situation (from `STATE`) | What the host does |
| --- | --- |
| Both monitors on my input | Extend |
| One monitor on my input | Mirror onto it (Windows: Duplicate) |
| No monitor on my input | Leave the layout alone |
| Screens off holds every monitor I was on | Sleep my displays |
| A monitor comes (back) to my input | Wake my displays |
| A deck key asks for me (`STATE.wake`), even if I already have the monitors or keyboard | Wake my displays |
| My displays wake locally (mouse, keyboard) during Screens off | Ask the core for those monitors back on my input |

The rules live in one place, `common/host_policy.h` (plain C, used by both
daemons), with tests in `common/test_host_policy.c`:

```
cc -I components/kvm_proto/include -I host/common host/common/test_host_policy.c && ./a.out
```

Each daemon answers every `STATE` with a `STATUS` (awake, displays on/asleep,
topology applied), and sends `KVM_F_GOING_DOWN` before the host sleeps. It finds
the core by broadcasting `KVM_ACT_GET_STATE` until a `STATE` comes back, then
talks to that address; `--core <ipv4>` skips the broadcast.

## Screens off

A Dell only takes DDC standby (VCP 0xD6 = 4) from the port it is showing, and
the core's DDC goes in through HDMI. So the core switches the monitor to HDMI
and then sends standby (see `firmware/core/README.md`). It latches Screens off
per monitor and flags it in `STATE.off` (with `mon[]` = `KVM_IN_OFF`, as
before), until a real input is requested for that monitor. The hosts that were
on those monitors also sleep their own displays, so a mouse or key press there
can ask for the monitors back.

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
`deskstrate-host-windows` artifact from the `host` workflow run):

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

Logs: `%ProgramData%\Deskstrate\service.log` and `agent.log` (service), or
`%LOCALAPPDATA%\Deskstrate\host.log` (standalone). Options `--no-topology` and
`--core <ipv4>` go after `--service` in the service's command line.

## macOS (Mac)

A LaunchAgent (`com.deskstrate.host`) in the user's session, restarted if it
exits. Needs the Xcode command line tools (`xcode-select --install`).

```
make -C host/mac install     # build, install to ~/Library/Application Support/Deskstrate, start
make -C host/mac uninstall
host/mac/build/deskstrate-host --list-displays   # serials, origins, mirroring
```

Log: `~/Library/Logs/Deskstrate/host.log`. The Dells are found by EDID serial
(810562882 left, 810959170 right); if `--list-displays` shows other serials,
pass `--left-serial` / `--right-serial` (add them to `ProgramArguments` in
`com.deskstrate.host.plist`). macOS may ask to allow it to find devices on the
local network: allow it, or it never hears the core.

Building it on the Mac (rather than downloading the CI artifact) avoids
Gatekeeper quarantine on an unsigned binary.
