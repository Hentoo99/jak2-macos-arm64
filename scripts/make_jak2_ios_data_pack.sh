#!/usr/bin/env bash
# Build the Jak 2 data pack the iOS app reads from its Documents/jak2 folder, from this checkout's
# extracted and compiled game data (your own disc; never commit or share the pack).
#
#   scripts/make_jak2_ios_data_pack.sh [OUT_DIR]          (default: out/ios-data/jak2)
#
# Files are APFS clones of the originals (cp -c), so the pack takes no extra disk space.
# Voice audio: VOICE_LANGS="ENG ITA" (default ENG; the game opens the English voices at boot even
# with sound off, so ENG is always included).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PACK="${1:-$ROOT/out/ios-data/jak2}"
OUT=out/jak2-arm64

[ -d "$OUT/iso" ] && [ -d "$OUT/fr3" ] && [ -d out/gles-shaders ] || {
  echo "missing $OUT/iso, $OUT/fr3 or out/gles-shaders: extract, (mi) and build the GLES runtime" >&2
  exit 1
}

rm -rf "$PACK"
mkdir -p "$PACK/$OUT/iso" "$PACK/$OUT/obj" "$PACK/$OUT/fr3" "$PACK/out/gles-shaders" \
  "$PACK/game/assets/fonts"

# Disc data used by the runtime: code/level archives, streams, music, sound banks, text, screens.
find "$OUT/iso" -maxdepth 1 -type f ! -name 'VAGWAD.*' -exec cp -c {} "$PACK/$OUT/iso/" \;
for lang in ENG ${VOICE_LANGS:-}; do
  cp -c "$OUT/iso/VAGWAD.$lang" "$PACK/$OUT/iso/"
done
# Data objects (textures, art) loaded by name at runtime; the code objects are in the archives.
find "$OUT/obj" -maxdepth 1 -type f -name '*.go' -exec cp -c {} "$PACK/$OUT/obj/" \;
cp -c "$OUT"/fr3/* "$PACK/$OUT/fr3/"
cp -c out/gles-shaders/*.vert out/gles-shaders/*.frag "$PACK/out/gles-shaders/"
cp -c game/assets/sdl_controller_db.txt "$PACK/game/assets/"
cp -c game/assets/fonts/* "$PACK/game/assets/fonts/" 2>/dev/null || true

du -sh "$PACK"
echo "pack ready: $PACK"
