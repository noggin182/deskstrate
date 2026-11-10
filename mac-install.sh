#!/bin/sh
# Updates the Mac host daemon: pulls this checkout's current branch, then builds, installs
# and restarts it (make -C host/mac install). Run from anywhere: ./mac-install.sh
set -e
cd "$(dirname "$0")"
git pull --ff-only
make -C host/mac install
