#!/bin/sh
# Copies the PolyendTrackerDAW remote script into Ableton's User Library.
# Restart Live afterwards, then choose "PolyendTrackerDAW" in
# Settings > Link, Tempo & MIDI > Control Surface.
set -eu

SRC="$(cd "$(dirname "$0")" && pwd)/PolyendTrackerDAW"
DEST_ROOT="${ABLETON_USER_LIBRARY:-$HOME/Music/Ableton/User Library}/Remote Scripts"
DEST="$DEST_ROOT/PolyendTrackerDAW"

mkdir -p "$DEST_ROOT"
if [ -f "$DEST/mapping.json" ]; then
    # Keep the user's edited mapping.
    cp "$DEST/mapping.json" "$DEST_ROOT/.PolyendTrackerDAW-mapping.json.bak"
fi
rm -rf "$DEST"
mkdir -p "$DEST"
cp "$SRC/__init__.py" "$SRC/PolyendTrackerDAW.py" "$SRC/mapping.json" "$DEST/"
if [ -f "$DEST_ROOT/.PolyendTrackerDAW-mapping.json.bak" ]; then
    mv "$DEST_ROOT/.PolyendTrackerDAW-mapping.json.bak" "$DEST/mapping.json"
    echo "Kept your existing mapping.json"
fi
echo "Installed to: $DEST"
echo "Restart Ableton Live, then select PolyendTrackerDAW as a Control Surface."
