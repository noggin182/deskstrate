#!/bin/sh
# Updates the Mac daemon: pulls this checkout's current branch, then builds, installs and
# restarts it (make install). Run it from anywhere: host/mac/update.sh
set -e
cd "$(dirname "$0")/../.."
git pull --ff-only
make -C host/mac install
