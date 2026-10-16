"""Automated art check: measures the captured shots against art/look.json `checks` and audits assets.

  python3 godot/tools/art_check.py [--no-capture]
Captures (tools/capture.sh) unless --no-capture, writes greyscale copies, then checks:
  values    greyscale mean (or blue/red ratio, measure=blue_over_red) of each region in checks.<shot> within [min, max]
  mipmaps   every imported texture under assets/ has mipmaps (missing mipmaps = shimmering, the "20-year-old" look)
  budgets   triangles of every generated GLB within checks.budgets
Writes docs/art-check.md and exits 1 if anything fails. Needs ImageMagick (`magick`).
"""
import json
import pathlib
import struct
import subprocess
import sys

GODOT = pathlib.Path(__file__).resolve().parents[1]
SHOTS = GODOT.parent / "docs" / "screenshots"
LOOK = json.loads((GODOT / "art" / "look.json").read_text())
CHECKS = LOOK["checks"]


def region_mean(png, rect):
    x, y, w, h = rect
    out = subprocess.run(["magick", str(png), "-crop", f"{w}x{h}+{x}+{y}", "+repage", "-colorspace", "gray",
                          "-format", "%[fx:mean]", "info:"], capture_output=True, text=True, check=True)
    return float(out.stdout)


def blue_over_red(png, rect):
    x, y, w, h = rect
    out = subprocess.run(["magick", str(png), "-crop", f"{w}x{h}+{x}+{y}", "+repage", "-format",
                          "%[fx:mean.b] %[fx:mean.r]", "info:"], capture_output=True, text=True, check=True)
    b, r = map(float, out.stdout.split())
    return b / max(r, 1e-6)


def glb_triangles(path):
    b = path.read_bytes()
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    tris = 0
    for mesh in j["meshes"]:
        for prim in mesh["primitives"]:
            acc = j["accessors"][prim["indices"]] if "indices" in prim else j["accessors"][prim["attributes"]["POSITION"]]
            tris += acc["count"] // 3
    return tris


def main():
    if "--no-capture" not in sys.argv:
        subprocess.run([str(GODOT / "tools" / "capture.sh")], check=True)
    rows, ok = [], True

    for shot in ("far", "mid"):
        png = SHOTS / f"godot_{shot}.png"
        subprocess.run(["magick", str(png), "-colorspace", "gray", str(SHOTS / f"godot_{shot}_grey.png")], check=True)
        for name, leaf in CHECKS[shot].items():
            spec = leaf["v"]
            m = blue_over_red(png, spec["rect"]) if spec.get("measure") == "blue_over_red" else region_mean(png, spec["rect"])
            passed = spec["min"] <= m <= spec["max"]
            ok &= passed
            rows.append(("value", f"{shot}.{name}", f"{m:.3f}", f"{spec['min']}-{spec['max']}", passed, leaf["why"]))

    imports = list((GODOT / "assets").rglob("*.jpg.import")) + list((GODOT / "assets").rglob("*.png.import"))
    no_mips = [p for p in imports if "mipmaps/generate=true" not in p.read_text()]
    ok &= not no_mips
    rows.append(("mipmaps", f"{len(imports)} textures", f"{len(no_mips)} without", "0", not no_mips,
                 ", ".join(p.name for p in no_mips[:5]) or "all mipmapped"))

    budgets = CHECKS["budgets"]["v"]
    for kind, budget in (("buildings", budgets["building"]), ("props", budgets["prop"]),
                         ("foliage", budgets["foliage"]), ("trees", budgets["foliage"])):
        for glb in sorted((GODOT / "assets" / "models" / kind).glob("*.glb")):
            t = glb_triangles(glb)
            passed = t <= budget
            ok &= passed
            if not passed or kind == "buildings":
                rows.append(("budget", f"{kind}/{glb.stem}", str(t), f"<= {budget}", passed, "triangles"))

    lines = ["# Art check", "", f"Result: **{'PASS' if ok else 'FAIL'}** (rerun: `python3 godot/tools/art_check.py`)", "",
             "| check | item | measured | target | pass | note |", "|---|---|---|---|---|---|"]
    lines += [f"| {a} | {b} | {c} | {d} | {'yes' if e else '**NO**'} | {f} |" for a, b, c, d, e, f in rows]
    report = GODOT.parent / "docs" / "art-check.md"
    report.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(0 if ok else 1)


main()
