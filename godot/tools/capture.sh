#!/bin/sh
# Renders the far/mid/dusk/river shots into $1 (default docs/screenshots), one Godot process per shot.
# One process per shot: macOS sometimes stops rendering a window partway through a multi-shot run while the user
# works in other windows, but a fresh run renders its shot. Each shot gets one retry. Exits non-zero on failure.
cd "$(dirname "$0")/.." || exit 1
OUT="${1:-$(cd .. && pwd)/docs/screenshots}"
for shot in godot_far godot_mid godot_far_dusk godot_far_night godot_river; do
	rm -f "$OUT/$shot.png"
	for attempt in 1 2; do
		fill=0; case $shot in godot_far|godot_mid) fill=1;; esac  # sun-off frame for tools/art_check.py shadow masks
		caffeinate -d godot --path . --always-on-top --resolution 480x270 --quit-after 6000 -- --capture="$OUT" --only=$shot --fill-pass=$fill >/dev/null 2>&1
		[ -f "$OUT/$shot.png" ] && break
	done
	[ -f "$OUT/$shot.png" ] || { echo "capture failed: $shot" >&2; exit 1; }
	echo "captured $OUT/$shot.png"
done
