#!/usr/bin/env bash
# Builds the optional headless test harness (Mac). Needs mGBA 0.10.5 source built as a static
# library first:
#   git clone --depth 1 --branch 0.10.5 https://github.com/mgba-emu/mgba.git ~/toolchains/src/mgba
#   cd ~/toolchains/src/mgba && cmake -S . -B build2 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_QT=OFF \
#     -DBUILD_SDL=OFF -DUSE_LUA=OFF -DUSE_FFMPEG=OFF -DUSE_EPOXY=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF \
#     -DUSE_DISCORD_RPC=OFF -DUSE_MINIZIP=OFF -DUSE_LIBZIP=OFF -DM_CORE_GB=OFF -DBUILD_GL=OFF \
#     -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF -DBUILD_STATIC=ON -DBUILD_SHARED=OFF && cmake --build build2 -j8
set -e
MGBA="${MGBA_SRC:-$HOME/toolchains/src/mgba}"
OUT="${1:-$HOME/toolchains/bin/gbaharness}"
mkdir -p "$(dirname "$OUT")"
BREW="$(brew --prefix)"
cc -O2 -o "$OUT" "$(dirname "$0")/harness.c" \
  -I"$MGBA/include" -I"$MGBA/build2/include" -I"$BREW/include" -DM_CORE_GBA -DUSE_PNG -DUSE_ZLIB -DUSE_LZMA -DENABLE_SCRIPTING -DUSE_DEBUGGERS \
  "$MGBA/build2/libmgba.a" -L"$BREW/lib" -lpng -lz -llzma -ledit -framework CoreFoundation
echo "Built $OUT"
