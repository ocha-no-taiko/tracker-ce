#!/bin/sh
# Builds a preview image of the DAW Control screen on the host (no hardware):
# the real drawing code with the sample DawControl.json and LiveTracker.ptfont.
#   sh tests/dawControl/preview.sh [out.png] [mockup.png]
# Needs uv (for Pillow). Fails if the screen would not fit in one frame.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT_DIR="${TMPDIR:-/tmp}"
PNG="${1:-$OUT_DIR/dawcontrol_screen.png}"
{
  echo '#pragma once'
  sed -n '/^enum mtInterfaceButtonsDef/,/^enum padStates/p' "$ROOT/core/interface/core/interfaceDefs.h" | sed '$d'
} > "$HERE/stub/core/interfaceDefs.h"
${CXX:-c++} -std=c++17 -Wall -Wextra -I"$HERE/screen_stub" -I"$HERE/stub" -I"$ROOT/core/interface" \
  -include cstdint "$HERE/screen_preview.cpp" "$ROOT/core/interface/dawControl/dawScreen.cpp" \
  "$ROOT/core/interface/dawControl/dawControlConfig.cpp" -o "$OUT_DIR/dawcontrol_screen_preview"
"$OUT_DIR/dawcontrol_screen_preview" "$ROOT/sd-card/LiveTracker.ptfont" "$ROOT/sd-card/DawControl.json" \
  "$OUT_DIR/dawcontrol_screen_list.txt" ${PREVIEW_NAMES:-}
uv run -q --with pillow python "$HERE/preview_screen.py" "$OUT_DIR/dawcontrol_screen_list.txt" \
  "$ROOT/sd-card/LiveTracker.ptfont" "$PNG" ${2:+"$2"}
echo "Preview: $PNG"
