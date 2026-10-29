#!/usr/bin/env bash
# Build the Jak 2 iOS app (signed AOT code + OpenGL ES on ANGLE) for the iOS Simulator, install it
# and launch it, reading the game data from this checkout.  See docs/ios/DECISIONI.md.
#
#   scripts/run_jak2_ios_simulator.sh [SIMULATOR_NAME]       (default: "iPad Pro 11-inch (M5)")
#
# Requires: out/aot/jak2/macos-arm64 (scripts/build_jak2_aot_macos.sh) and the prebuilt ANGLE for
# the simulator in ANGLE_DIR.  Screenshots: xcrun simctl io booted screenshot shot.png
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SIM_NAME="${1:-iPad Pro 11-inch (M5)}"
BUILD_DIR="build-ios-sim"
ANGLE_DIR="${ANGLE_DIR:-$HOME/jak-ios/deps/angle/chromium-7151_rev1/iphonesimulator-arm64}"
GEN_DIR="$ROOT/out/aot/jak2/macos-arm64"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  cmake -G Ninja -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    "-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_SYSTEM_NAME;CMAKE_OSX_SYSROOT;CMAKE_OSX_ARCHITECTURES" \
    -DOPENGOAL_EXECUTION_MODE=AOT -DOPENGOAL_GRAPHICS_BACKEND=GLES \
    -DOPENGOAL_ANGLE_DIR="$ANGLE_DIR" -DOPENGOAL_AOT_GENERATED_DIR="$GEN_DIR" \
    -DOG_BUILD_HOST_TOOLS=OFF -DSTATICALLY_LINK=ON
fi
ninja -C "$BUILD_DIR" gk
APP="$BUILD_DIR/game/gk.app"
codesign --force --deep -s - "$APP" >/dev/null

DEV=$(xcrun simctl list devices available | grep -F "$SIM_NAME (" | head -1 | sed -E 's/.*\(([0-9A-F-]{36})\).*/\1/')
if [ -z "$DEV" ]; then
  echo "no available simulator named '$SIM_NAME'" >&2
  exit 1
fi
xcrun simctl boot "$DEV" 2>/dev/null || true
xcrun simctl bootstatus "$DEV" -b >/dev/null
xcrun simctl install "$DEV" "$APP"
xcrun simctl launch --terminate-running-process "$DEV" org.opengoal.jak2 \
  -g jak2 --proj-path "$ROOT" -- -boot -fakeiso -nosound
echo "launched on $SIM_NAME ($DEV); game log: $ROOT/log/"
