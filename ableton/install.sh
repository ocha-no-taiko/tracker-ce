#!/bin/sh
# Copies the PolyendTrackerDAW remote script into Ableton's User Library.
# Restart Live afterwards, then choose "PolyendTrackerDAW" in
# Settings > Link, Tempo & MIDI > Control Surface.
#
# An existing mapping.json is kept (your own button assignments). The new
# defaults are then saved next to it as mapping.default.json.
# To replace it with the new defaults instead:
#     RESET_MAPPING=1 sh ableton/install.sh
set -eu

SRC="$(cd "$(dirname "$0")" && pwd)/PolyendTrackerDAW"
DEST_ROOT="${ABLETON_USER_LIBRARY:-$HOME/Music/Ableton/User Library}/Remote Scripts"
DEST="$DEST_ROOT/PolyendTrackerDAW"
BACKUP="$DEST_ROOT/.PolyendTrackerDAW-mapping.json.bak"
RESET_MAPPING="${RESET_MAPPING:-0}"

mkdir -p "$DEST_ROOT"
rm -f "$BACKUP"
if [ -f "$DEST/mapping.json" ] && [ "$RESET_MAPPING" != "1" ]; then
    cp "$DEST/mapping.json" "$BACKUP"
fi
rm -rf "$DEST"
mkdir -p "$DEST"
cp "$SRC/__init__.py" "$SRC/PolyendTrackerDAW.py" "$SRC/keysender.js" "$SRC/mapping.json" "$DEST/"
if [ -f "$BACKUP" ]; then
    if cmp -s "$BACKUP" "$SRC/mapping.json"; then
        rm -f "$BACKUP"
    else
        cp "$SRC/mapping.json" "$DEST/mapping.default.json"
        mv "$BACKUP" "$DEST/mapping.json"
        echo "Kept your existing mapping.json (new defaults: mapping.default.json)."
        echo "To use the new defaults: RESET_MAPPING=1 sh ableton/install.sh"
    fi
fi
echo "Installed to: $DEST"
echo "Restart Ableton Live, then select PolyendTrackerDAW as a Control Surface."
