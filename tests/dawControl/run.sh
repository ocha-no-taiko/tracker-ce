#!/bin/sh
# Host-side test of the DAW Control JSON parser (no hardware needed).
#   sh tests/dawControl/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/dawcontrol_parser_test"
# Button ids are taken from the real header so the test follows firmware changes.
{
  echo '#pragma once'
  sed -n '/^enum mtInterfaceButtonsDef/,/^enum padStates/p' "$ROOT/core/interface/core/interfaceDefs.h" | sed '$d'
} > "$HERE/stub/core/interfaceDefs.h"
${CXX:-c++} -std=c++17 -Wall -Wextra -DSDJSON="\"$ROOT/sd-card/DawControl.json\"" \
  -I"$HERE/stub" -I"$ROOT/core/interface" -include cstdint \
  "$HERE/test_config_parser.cpp" "$ROOT/core/interface/dawControl/dawControlConfig.cpp" -o "$OUT"
"$OUT"
