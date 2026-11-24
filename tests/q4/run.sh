#!/bin/sh
# Host-side test of the Q-4 JSON parser (no hardware needed).
#   sh tests/q4/run.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DAW="$ROOT/tests/dawControl"
OUT="${TMPDIR:-/tmp}/q4_parser_test"
mkdir -p "$DAW/stub/core"
{
  echo '#pragma once'
  sed -n '/^enum mtInterfaceButtonsDef/,/^enum padStates/p' "$ROOT/core/interface/core/interfaceDefs.h" | sed '$d'
} > "$DAW/stub/core/interfaceDefs.h"
${CXX:-c++} -std=c++17 -Wall -Wextra -DSDJSON="\"$ROOT/sd-card/Q4Control.json\"" \
  -I"$DAW/stub" -I"$ROOT/core/interface" -include cstdint \
  "$HERE/test_config_parser.cpp" "$ROOT/core/interface/q4/q4Config.cpp" -o "$OUT"
"$OUT"
