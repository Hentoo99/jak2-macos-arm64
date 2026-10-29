#!/usr/bin/env bash
# Build the Jak 2 signed AOT runtime for macOS ARM64 (no JIT), see docs/ios/DECISIONI.md.
#
#   scripts/build_jak2_aot_macos.sh
#
# Requires build-arm64-release (goalc, jak2-aot-emit) and an up to date
# out/jak2-arm64 compiled with the default 'page' ARM64 code model.
# FUNCTION_TYPE must match the runtime; gk prints the right value and stops if
# it does not.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OBJ_DIR="out/jak2-arm64/obj"
GEN_DIR="$ROOT/out/aot/jak2/macos-arm64"
BUILD_DIR="build-arm64-aot"
FUNCTION_TYPE="${FUNCTION_TYPE:-0x16fde4}"

python3 -I scripts/arm64_code_model_audit.py --require-page "$OBJ_DIR" | tail -1

# Every GOAL object except the kernel unit-test fixture.
LIST="$(mktemp)"
trap 'rm -f "$LIST"' EXIT
(cd "$OBJ_DIR" && ls *.o | sed 's/\.o$//' | grep -v '^kernel-test$') > "$LIST"
build-arm64-release/goalc/jak2-aot-emit --obj-dir "$OBJ_DIR" --module-list "$LIST" \
  --function-type "$FUNCTION_TYPE" --output-dir "$GEN_DIR"

if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  cmake -G Ninja -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" -DOPENGOAL_EXECUTION_MODE=AOT \
    -DOPENGOAL_GRAPHICS_BACKEND=OPENGL -DOPENGOAL_AOT_GENERATED_DIR="$GEN_DIR" \
    -DOG_BUILD_HOST_TOOLS=OFF
fi
ninja -C "$BUILD_DIR" gk
echo "built $BUILD_DIR/game/gk.app (run: $BUILD_DIR/game/gk.app/Contents/MacOS/gk -g jak2 --proj-path . -- -boot -fakeiso -debug)"
