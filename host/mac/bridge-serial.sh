#!/bin/sh
# Prints the USB serial number of the Mac's serial bridge (firmware/bridge) for the
# LaunchAgent's --bridge-serial. In order:
#   1. $BRIDGE_SERIAL, if set (make install BRIDGE_SERIAL=D4:05:92:4B:67:D0);
#   2. the one the installed LaunchAgent already has, so a reinstall works undocked;
#   3. the only Espressif USB serial port plugged in (deskstrate-host --list-bridges).
#   bridge-serial.sh <deskstrate-host binary> <installed LaunchAgent plist>
set -e
bin=$1
agent=$2
if [ -n "$BRIDGE_SERIAL" ]; then
    echo "$BRIDGE_SERIAL"
    exit 0
fi
if [ -f "$agent" ]; then
    sn=$(plutil -extract ProgramArguments.2 raw -o - "$agent" 2>/dev/null || true)
    case "$sn" in
        ""|@*|-*) ;;
        *) echo "$sn"; exit 0 ;;
    esac
fi
found=$("$bin" --list-bridges)
count=$(printf '%s\n' "$found" | grep -c . || true)
if [ "$count" = 1 ]; then
    printf '%s\n' "$found" | cut -d' ' -f1
    exit 0
fi
if [ "$count" = 0 ]; then
    echo "bridge-serial.sh: no bridge plugged in. Plug it in, or: make install BRIDGE_SERIAL=<its MAC>" >&2
else
    echo "bridge-serial.sh: more than one ESP plugged in; pick one with make install BRIDGE_SERIAL=<serial>:" >&2
    printf '%s\n' "$found" >&2
fi
exit 1
