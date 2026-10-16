"""Automated art check: measures the captured shots against art/look.json `checks` and audits assets.

  python3 godot/tools/art_check.py [--no-capture]
Captures (tools/capture.sh) unless --no-capture, writes greyscale copies, then checks:
  values    per checks.<shot>: lit/shadow means and shadow tint from a sun-on vs sun-off pair (follows the shadows),
            contact floor (1st percentile of the blurred frame), or a greyscale region mean; each within [min, max]
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


def pixels(png, size="480x270", channels="gray"):
    """Raw 8-bit pixels at a reduced size: list of floats (gray) or (r, g, b) tuples."""
    out = subprocess.run(["magick", str(png), "-resize", f"{size}!", "-depth", "8",
                          "-colorspace", "gray" if channels == "gray" else "sRGB", f"{channels}:-"],
                         capture_output=True, check=True).stdout
    if channels == "gray":
        return [b / 255 for b in out]
    return [(out[i] / 255, out[i + 1] / 255, out[i + 2] / 255) for i in range(0, len(out), 3)]


def _sat(c):
    mx, mn = max(c), min(c)
    l = (mx + mn) / 2
    return 0.0 if mx == mn else (mx - mn) / (1 - abs(2 * l - 1) + 1e-6)


def masked(full_png, fill_png):
    """Classify pixels by how much the sun adds, ratio = full / sun-off in linear light. Shadowed ground still gets
    some sun through bounce light (measured ratios ~1-3); sunlit ground 5-60. Pixels in between are left out."""
    full, fill = pixels(full_png), pixels(fill_png)
    rgb_full = pixels(full_png, channels="rgb")
    light = pixels(str(full_png).replace(".png", "_light.png"), channels="rgb")
    lit, shadow = [], []
    for i, (f, s) in enumerate(zip(full, fill)):
        if s <= 0.03 or f <= 0.02:
            continue
        ratio = f ** 2.2 / s ** 2.2
        if ratio > 5.0:
            lit.append(i)
        elif ratio < 3.0:
            shadow.append(i)
    mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
    return {"lit_mean": mean([full[i] for i in lit]), "shadow_mean": mean([full[i] for i in shadow]),
            # light colour in shadow, measured on the lighting-only pass (white albedo), so material colour is excluded
            "shadow_tint": mean([light[i][2] / max(light[i][0], 1e-3) for i in shadow]),
            "shadow_green": mean([light[i][1] / max((light[i][0] + light[i][2]) / 2, 1e-3) for i in shadow]),
            "lit_saturation": mean([_sat(rgb_full[i]) for i in lit]),
            # key:fill = light reaching sunlit vs shadowed surfaces, linear, on the lighting-only pass (albedo-free)
            "key_fill": (mean([(sum(light[i]) / 3) ** 2.2 for i in lit])
                         / max(mean([(sum(light[i]) / 3) ** 2.2 for i in shadow]), 1e-6)),
            "lit_share": len(lit) / len(full), "shadow_share": len(shadow) / len(full)}


def contact_floor(png):
    out = subprocess.run(["magick", str(png), "-colorspace", "gray", "-blur", "0x8", "-resize", "480x270!",
                          "-depth", "8", "gray:-"], capture_output=True, check=True).stdout
    vals = sorted(b / 255 for b in out)
    return vals[len(vals) // 100]


def macro_sd(png, rect):
    x, y, w, h = rect
    out = subprocess.run(["magick", str(png), "-colorspace", "gray", "-blur", "0x16", "-crop", f"{w}x{h}+{x}+{y}",
                          "+repage", "-format", "%[fx:standard_deviation]", "info:"], capture_output=True, text=True, check=True)
    return float(out.stdout)


def speckle(png, rect):
    x, y, w, h = rect
    raw = subprocess.run(["magick", str(png), "-crop", f"{w}x{h}+{x}+{y}", "+repage", "-colorspace", "gray", "-depth", "8",
                          "gray:-"], capture_output=True, check=True).stdout
    vals = sorted(raw)
    med = vals[len(vals) // 2]
    return sum(1 for v in raw if v > med + 0.15 * 255) / len(raw)


def saturation(png, rect):
    x, y, w, h = rect
    out = subprocess.run(["magick", str(png), "-crop", f"{w}x{h}+{x}+{y}", "+repage", "-colorspace", "HSL", "-channel", "G",
                          "-separate", "-format", "%[fx:mean]", "info:"], capture_output=True, text=True, check=True)
    return float(out.stdout)


def empty_share(png):
    """Share of ~1.3 m x 1.3 m blocks (160 px at the far view) with almost no tonal detail: empty patches of ground."""
    raw = subprocess.run(["magick", str(png), "-colorspace", "gray", "-blur", "0x3", "-resize", "480x270!", "-depth", "8",
                          "gray:-"], capture_output=True, check=True).stdout
    w, h, b, flat, tot = 480, 270, 40, 0, 0
    for by in range(0, h - b + 1, b):
        for bx in range(0, w - b + 1, b):
            vals = [raw[(by + y) * w + bx + x] / 255 for y in range(b) for x in range(b)]
            m = sum(vals) / len(vals)
            sd = (sum((v - m) ** 2 for v in vals) / len(vals)) ** 0.5
            tot += 1
            flat += sd < 0.04
    return flat / tot


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
        stats = masked(png, SHOTS / f"godot_{shot}_fill.png")
        rows.append(("info", f"{shot} masks", f"lit {stats['lit_share']:.0%}, shadow {stats['shadow_share']:.0%}",
                     "", True, "share of the frame classified lit / in shadow"))
        for name, leaf in CHECKS[shot].items():
            spec = leaf["v"]
            kind = spec.get("measure", "rect")
            m = (stats[kind] if kind in stats else contact_floor(png) if kind == "contact_floor"
                 else macro_sd(png, spec["rect"]) if kind == "macro_sd" else speckle(png, spec["rect"]) if kind == "speckle"
                 else saturation(png, spec["rect"]) if kind == "saturation"
                 else empty_share(png) if kind == "empty_share"
                 else blue_over_red(png, spec["rect"]) if kind == "blue_over_red" else region_mean(png, spec["rect"]))
            passed = spec["min"] <= m <= spec["max"]
            ok &= passed
            rows.append(("value", f"{shot}.{name}", f"{m:.3f}", f"{spec['min']}-{spec['max']}", passed, leaf["why"]))

    for shot_name, png_name in (("night", "godot_far_night.png"), ("dusk", "godot_far_dusk.png")):
        png = SHOTS / png_name
        for name, leaf in CHECKS.get(shot_name, {}).items():
            if name.startswith("_"):
                continue
            spec = leaf["v"]
            if spec.get("measure") == "torch_pool":
                m = region_mean(png, spec["rect"]) / max(region_mean(png, [0, 0, 1920, 1080]), 1e-3)
            else:
                m = region_mean(png, spec["rect"])
            passed = spec["min"] <= m <= spec["max"]
            ok &= passed
            rows.append(("value", f"{shot_name}.{name}", f"{m:.3f}", f"{spec['min']}-{spec['max']}", passed, leaf["why"]))

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
