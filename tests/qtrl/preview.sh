#!/bin/sh
# Builds preview images of every Q.T.R.L. tab on the host (no hardware): the
# real drawing code with the sample QtrlControl.json and Qtrl.ptfont.
#   sh tests/qtrl/preview.sh [out_dir] [mockup.png]
# Needs uv (for Pillow). Fails if a tab would not fit in one frame.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DAW="$ROOT/tests/dawControl"
OUT_DIR="${1:-${TMPDIR:-/tmp}/qtrl_preview}"
mkdir -p "$OUT_DIR" "$DAW/stub/core"
{
  echo '#pragma once'
  sed -n '/^enum mtInterfaceButtonsDef/,/^enum padStates/p' "$ROOT/core/interface/core/interfaceDefs.h" | sed '$d'
} > "$DAW/stub/core/interfaceDefs.h"
${CXX:-c++} -std=c++17 -Wall -Wextra -I"$DAW/screen_stub" -I"$DAW/stub" -I"$ROOT/core/interface" \
  -include cstdint "$HERE/screen_preview.cpp" "$ROOT/core/interface/qtrl/qtrlScreen.cpp" \
  "$ROOT/core/interface/qtrl/qtrlConfig.cpp" -o "$OUT_DIR/qtrl_screen_preview"
"$OUT_DIR/qtrl_screen_preview" "$ROOT/sd-card/Qtrl.ptfont" "$ROOT/sd-card/QtrlControl.json" "$OUT_DIR/"
for list in "$OUT_DIR"/*.txt; do
  name="$(basename "$list" .txt)"
  uv run -q --with pillow python "$DAW/preview_screen.py" "$list" "$ROOT/sd-card/Qtrl.ptfont" \
    "$OUT_DIR/$name.png" ${2:+"$2"}
done
echo "Previews: $OUT_DIR"
