#!/usr/bin/env bash
# Emit signed-module assembly for a set of ARM64 GOAL objects, link it into a
# test executable, and check the linked code against the objects
# (test/aot/signed_module_roundtrip.cpp).
#
#   scripts/aot_signed_roundtrip.sh WORK_DIR [MODULE_LIST_FILE]
#
# Without a module list, every object in out/jak2-arm64/obj is used.
# FUNCTION_TYPE may be set in the environment (any valid value works here).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:?usage: $0 WORK_DIR [MODULE_LIST_FILE]}"
LIST="${2:-}"
OBJ_DIR="$ROOT/out/jak2-arm64/obj"
EMIT="$ROOT/build/goalc/jak2-aot-emit"
FUNCTION_TYPE="${FUNCTION_TYPE:-0x1000}"

mkdir -p "$WORK"
if [ -z "$LIST" ]; then
  LIST="$WORK/modules.txt"
  (cd "$OBJ_DIR" && ls *.o | sed 's/\.o$//') > "$LIST"
fi

"$EMIT" --obj-dir "$OBJ_DIR" --module-list "$LIST" --function-type "$FUNCTION_TYPE" \
  --output-dir "$WORK/emit"

mkdir -p "$WORK/obj"
clang -c -arch arm64 "$WORK/emit/arena-layout.S" -o "$WORK/obj/arena-layout.o"
ls "$WORK/emit/modules/"*.S | xargs -P "$(sysctl -n hw.ncpu)" -I{} sh -c \
  'clang -c -arch arm64 "$1" -o "$2/obj/$(basename "$1" .S).o"' _ {} "$WORK"
clang -c -arch arm64 -I"$ROOT" "$WORK/emit/module-index.c" -o "$WORK/obj/module-index.o"
clang++ -std=c++20 -O1 -arch arm64 -I"$ROOT" "$ROOT/test/aot/signed_module_roundtrip.cpp" \
  "$WORK/obj/arena-layout.o" "$WORK/obj/module-index.o" \
  $(ls "$WORK/obj/"goal_*.o) \
  -Wl,-segprot,__ARENA,rw,rw -Wl,-segprot,__GOAL,rx,rx \
  -o "$WORK/roundtrip"

"$WORK/roundtrip" "$OBJ_DIR"
