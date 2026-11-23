#!/usr/bin/env bash
# Builds the GBA game (gba/snowberry.gba). Mac/Linux version of build.ps1 - keep them doing the same thing.
#   ./build.sh          build
#   ./build.sh -r       build, then open the ROM in mGBA
#   ./build.sh clean    delete build output
set -e
cd "$(dirname "$0")"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
export PATH="$DEVKITPRO/tools/bin:$PATH"
[ -x "$DEVKITARM/bin/arm-none-eabi-gcc" ] || { echo "devkitARM not found at $DEVKITARM (see README: GBA game setup)"; exit 1; }
# LIBBUTANOABS can point at another Butano checkout (handy for extra git worktrees).
if [ -z "${LIBBUTANOABS:-}" ]; then [ -f butano/butano/butano.mak ] || git submodule update --init; fi
if [ "${1:-}" = "clean" ]; then make clean; exit 0; fi
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
make -j"$JOBS"
echo "Built $(pwd)/snowberry.gba"
if [ "${1:-}" = "-r" ]; then
  if [ "$(uname)" = "Darwin" ]; then open -a mGBA snowberry.gba; else (mgba-qt snowberry.gba >/dev/null 2>&1 &); fi
fi
