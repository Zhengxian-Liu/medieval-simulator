"""A/B any look.json values against the current look without committing them.

  python3 godot/tools/ab.py <label> <path=value> [<path=value> ...] [--shots godot_far,godot_mid]

<path> is a look.json path to a leaf (`sun.angular_distance`) or into its value (`stylise.surface.brush`); <value> is
JSON. look.json is restored afterwards, even on failure. Renders the B shots into docs/screenshots/ab/<label>/ and
writes <shot>_ab.jpg there: the current shot (A, docs/screenshots/<shot>.png, so run art_check.py first) left, B right.
"""
import json
import os
import pathlib
import subprocess
import sys

GODOT = pathlib.Path(__file__).resolve().parents[1]
LOOK = GODOT / "art" / "look.json"
SHOTS = GODOT.parent / "docs" / "screenshots"


def set_path(doc, path, value):
    node, keys = doc, path.split(".")
    while "v" not in node:  # walk groups down to the leaf ({"v": ..., "why": ...})
        node = node[keys.pop(0)]
    if not keys:
        node["v"] = value
        return
    node = node["v"]
    for k in keys[:-1]:
        node = node[k]
    node[keys[-1]] = value


def main():
    args = sys.argv[1:]
    shots = "godot_far,godot_mid"
    if "--shots" in args:
        i = args.index("--shots")
        shots = args[i + 1]
        del args[i:i + 2]
    label, pairs = args[0], args[1:]
    original = LOOK.read_text()
    doc = json.loads(original)
    for pair in pairs:
        path, value = pair.split("=", 1)
        set_path(doc, path, json.loads(value))
    out = SHOTS / "ab" / label
    out.mkdir(parents=True, exist_ok=True)
    try:
        LOOK.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n")
        subprocess.run([str(GODOT / "tools" / "capture.sh"), str(out)], check=True,
                       env={**os.environ, "SHOTS": shots.replace(",", " ")})
    finally:
        LOOK.write_text(original)
    (out / "settings.txt").write_text("\n".join(pairs) + "\n")
    for shot in shots.split(","):
        subprocess.run(["magick", str(SHOTS / f"{shot}.png"), str(out / f"{shot}.png"), "-resize", "960x", "+append",
                        "-quality", "88", str(out / f"{shot}_ab.jpg")], check=True)
        print(out / f"{shot}_ab.jpg")


main()
