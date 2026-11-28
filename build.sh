#!/usr/bin/env bash
# Mac/Linux version of build.ps1. Keep the two scripts doing the same thing.
#   ./build.sh                -> build every game in src/ (each folder except common/ is one ROM)
#   ./build.sh -r pingpong    -> build all, then open build/pingpong.gbc in mGBA
# GBDK location: $GBDK_HOME, or ~/gbdk by default.
set -u
cd "$(dirname "$0")"
run=""
[ "${1:-}" = "-r" ] || [ "${1:-}" = "-Run" ] && run="${2:-}"

lcc="${GBDK_HOME:-$HOME/gbdk}/bin/lcc"
[ -x "$lcc" ] || { echo "GBDK not found at $lcc (set GBDK_HOME or install to ~/gbdk; see README)"; exit 1; }
mkdir -p build

# mGBA can lock or hold the open ROM, so close it before rebuilding
pkill -x mGBA 2>/dev/null && sleep 0.3

failed=0
for dir in src/*/; do
  name=$(basename "$dir")
  [ "$name" = "common" ] && continue
  title=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]' | cut -c1-15)
  # Optional extra compiler flags for one game (e.g. a cartridge with battery save): src/<game>/lcc_flags.txt
  extra=""
  [ -f "src/$name/lcc_flags.txt" ] && extra=$(tr -d '\r' < "src/$name/lcc_flags.txt")
  # shellcheck disable=SC2086
  if "$lcc" -Wm-yc "-Wm-yn$title" $extra -o "build/$name.gbc" "src/$name/main.c"; then
    echo "Built build/$name.gbc"
  else
    echo "BUILD FAILED: $name"; failed=1
  fi
done
[ $failed -eq 0 ] || exit 1

if [ -n "$run" ]; then
  rom="build/$run.gbc"
  [ -f "$rom" ] || { echo "No such game: $run"; exit 1; }
  if [ "$(uname)" = "Darwin" ]; then open -a mGBA "$rom"; else (mgba-qt "$rom" >/dev/null 2>&1 &); fi
fi
