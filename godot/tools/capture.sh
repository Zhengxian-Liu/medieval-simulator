#!/bin/sh
# Renders the far/mid/dusk/river shots into $1 (default docs/screenshots), one Godot process per shot.
# One process per shot: macOS sometimes stops rendering a window partway through a multi-shot run while the user
# works in other windows, but a fresh run renders its shot. Each shot gets two retries. Exits non-zero on failure.
cd "$(dirname "$0")/.." || exit 1
OUT="${1:-$(cd .. && pwd)/docs/screenshots}"
# Reimport first: running the game uses the cached imports, so a swapped texture or rebuilt GLB never reached the shots.
godot --headless --path . --import >/dev/null 2>&1 || { echo "import failed" >&2; exit 1; }
# SHOTS="godot_mid" tools/capture.sh  re-renders only those shots (quick A/B of one value); default: all
for shot in ${SHOTS:-godot_far godot_mid godot_far_dusk godot_far_night godot_far_morning godot_river}; do
	rm -f "$OUT/$shot.png" "$OUT/${shot}_fill.png" "$OUT/${shot}_light.png" "$OUT/${shot}_id.png"
	for attempt in 1 2 3; do  # intermittent GPU fence timeouts (2026-10-17): retry
		fill=0; case $shot in godot_far|godot_mid|godot_far_dusk|godot_far_morning) fill=1;; esac  # sun-off + lighting-only frames for tools/art_check.py masks
		caffeinate -d godot --path . --always-on-top --resolution 480x270 --quit-after 6000 -- --capture="$OUT" --only=$shot --fill-pass=$fill >/dev/null 2>&1
		# done only when every pass landed: a GPU fence timeout once left the ID pass missing while the frame existed
		[ -f "$OUT/$shot.png" ] && { [ $fill = 0 ] || [ -f "$OUT/${shot}_id.png" ]; } && break
	done
	[ -f "$OUT/$shot.png" ] && { [ $fill = 0 ] || [ -f "$OUT/${shot}_id.png" ]; } || { echo "capture failed: $shot" >&2; exit 1; }
	echo "captured $OUT/$shot.png"
done
