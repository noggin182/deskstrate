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

The Dells ignore DDC standby (VCP 0xD6 = 4) while a host is sending them a
signal. So the core latches Screens off per monitor and flags it in
`STATE.off` (with `mon[]` = `KVM_IN_OFF`, as before). The hosts on those
monitors sleep their displays, the monitors lose signal and go to standby by
themselves. Any real input for a monitor clears its latch. An HDMI guest has
no daemon, so a monitor showing a guest stays on.

## Windows (PC)

A tray app (`deskstrate-host.exe`) started at logon. It has to run in the
user's session: sleeping/waking the displays and switching Extend/Duplicate
can't be done from a service.

Build (Visual Studio 2022 with the C++ workload, or take the
`deskstrate-host-windows` artifact from the `host` workflow run):

```
cmake -S host/windows -B host/windows/build -A x64
cmake --build host/windows/build --config Release
```

Install (current user, no admin; puts a shortcut in the Startup folder and
starts it):

```
powershell -ExecutionPolicy Bypass -File host\windows\install.ps1 -Exe host\windows\build\Release\deskstrate-host.exe
```

The first run asks Windows Firewall to let it listen on UDP 18200: allow it on
Private networks. Log: `%LOCALAPPDATA%\Deskstrate\host.log` (also from the tray
icon's menu). Options: `--no-topology`, `--core <ipv4>` (edit the shortcut).

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
