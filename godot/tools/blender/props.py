"""Yard props, each a function building one asset at the origin from the shared kit. PROPS maps asset name ->
builder; add a prop by writing its function and one PROPS entry, then `build_asset.py -- prop <name>`."""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

import kit
from kit import RNG, lathe, log


def barrel(origin=(0, 0, 0)):
    o = Vector(origin)
    h = 0.95
    prof = [(0.27, 0.0), (0.31, h * 0.2), (0.335, h * 0.5), (0.31, h * 0.8), (0.27, h)]
    lathe("barrel_staves", prof, "timber", segments=18, location=o)
    for z in (0.08, 0.3, 0.65, 0.87):
        r = 0.275 + 0.06 * math.sin(z / h * math.pi) + 0.012
        lathe(f"hoop_{z}", [(r, z * h - 0.03), (r, z * h + 0.03)], "iron", segments=18, cap=False, location=o)


def crate(origin=(0, 0, 0), size=0.7):
    o = Vector(origin)
    s = size
    for k in range(4):  # side slats with gaps
        z = 0.08 + k * (s - 0.1) / 4
        for side in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            sx, sy = side
            dims = (s, 0.025, s / 4 - 0.025) if sx == 0 else (0.025, s, s / 4 - 0.025)
            bm = kit.cube_bm(dims, bevel=0.004)
            kit.new_object(f"slat_{k}_{sx}_{sy}", bm, "timber", grain_axis=0 if sx == 0 else 1,
                          location=o + Vector((sx * s / 2, sy * s / 2, z + s / 8)))
    for sx in (-1, 1):  # corner battens and lid
        for sy in (-1, 1):
            bm = kit.cube_bm((0.05, 0.05, s), bevel=0.006)
            kit.new_object(f"batten_{sx}_{sy}", bm, "timber", grain_axis=2, location=o + Vector((sx * (s / 2 - 0.02), sy * (s / 2 - 0.02), s / 2)))
    bm = kit.cube_bm((s + 0.02, s + 0.02, 0.03), bevel=0.005)
    kit.new_object("lid", bm, "timber", grain_axis=0, location=o + Vector((0, 0, s + 0.015)))


def woodpile(origin=(0, 0, 0), length=2.4, rows=5):
    o = Vector(origin)
    for r in range(rows):
        count = int(length / 0.21) - r
        for i in range(count):
            x = -length / 2 + 0.11 + i * 0.21 + r * 0.105 + RNG.uniform(-0.02, 0.02)
            z = 0.1 + r * 0.18
            rad = RNG.uniform(0.08, 0.11)
            y0 = RNG.uniform(-0.05, 0.05)
            log(f"log_{r}_{i}", o + Vector((x, -0.25 + y0, z)), o + Vector((x, 0.25 + y0, z)), rad)
    for sx in (-1, 1):  # end stakes
        kit.beam(f"stake_{sx}", o + Vector((sx * (length / 2 + 0.05), 0, 0)), o + Vector((sx * (length / 2 + 0.05), 0, 1.05)), section=0.08)


def wheel(name, center, radius=0.48):
    c = Vector(center)
    rim = lathe(f"{name}_rim", [(radius, -0.04), (radius, 0.04), (radius - 0.06, 0.04), (radius - 0.06, -0.04), (radius, -0.04)],
                "timber", segments=24, cap=False)
    rim.rotation_euler = (math.pi / 2, 0, 0)
    rim.location = c
    for k in range(8):
        a = k / 8 * math.tau
        tip = c + Vector((math.cos(a) * (radius - 0.05), 0, math.sin(a) * (radius - 0.05)))
        kit.beam(f"{name}_spoke_{k}", c, tip, section=0.035, warp=0.0)
    hub = lathe(f"{name}_hub", [(0.07, -0.09), (0.08, 0.0), (0.07, 0.09)], "timber", segments=10)
    hub.rotation_euler = (math.pi / 2, 0, 0)
    hub.location = c


def handcart(origin=(0, 0, 0)):
    o = Vector(origin)
    bed_z = 0.62
    for k in range(6):  # bed planks
        bm = kit.cube_bm((1.5, 0.15, 0.04), bevel=0.005)
        kit.new_object(f"bed_{k}", bm, "timber", grain_axis=0, location=o + Vector((0, -0.4 + k * 0.16, bed_z)))
    for sy in (-1, 1):  # side boards and handles
        bm = kit.cube_bm((1.5, 0.04, 0.28), bevel=0.005)
        kit.new_object(f"side_{sy}", bm, "timber", grain_axis=0, location=o + Vector((0, sy * 0.47, bed_z + 0.16)))
        kit.beam(f"handle_{sy}", o + Vector((0.7, sy * 0.42, bed_z - 0.03)), o + Vector((1.75, sy * 0.36, 0.32)), section=0.06)
        wheel(f"wheel_{sy}", o + Vector((-0.15, sy * 0.58, 0.48)))
    kit.beam("axle", o + Vector((-0.15, -0.62, 0.48)), o + Vector((-0.15, 0.62, 0.48)), section=0.06)
    kit.beam("prop_leg", o + Vector((0.75, 0, bed_z - 0.04)), o + Vector((0.75, 0, 0.0)), section=0.06)


def wattle_fence(origin=(0, 0, 0), length=2.4):
    o = Vector(origin)
    stakes = int(length / 0.4) + 1
    for i in range(stakes):
        x = -length / 2 + i * length / (stakes - 1)
        kit.beam(f"stake_{i}", o + Vector((x, 0, -0.05)), o + Vector((x + RNG.uniform(-0.03, 0.03), RNG.uniform(-0.02, 0.02), 1.05)), section=0.05)
    for row in range(9):  # woven rods passing alternately in front of and behind the stakes
        z = 0.15 + row * 0.095
        bm = bmesh.new()
        segs = (stakes - 1) * 4
        verts = []
        for k in range(segs + 1):
            x = -length / 2 + k * length / segs
            y = 0.035 * math.sin(k / 4 * math.pi + row * math.pi)
            for a in range(5):
                ang = a / 5 * math.tau
                verts.append(bm.verts.new((x, y + math.cos(ang) * 0.018, z + math.sin(ang) * 0.018)))
        for k in range(segs):
            for a in range(5):
                b = (a + 1) % 5
                bm.faces.new([verts[k * 5 + a], verts[k * 5 + b], verts[(k + 1) * 5 + b], verts[(k + 1) * 5 + a]])
        kit.new_object(f"rod_{row}", bm, "timber", grain_axis=0, location=o)


def rail_fence(origin=(0, 0, 0), length=3.0):
    o = Vector(origin)
    tops = {}
    for sx in (-1, 1):  # old posts lean a few degrees and are not the same height
        top = o + Vector((sx * length / 2 + RNG.uniform(-0.08, 0.08), RNG.uniform(-0.08, 0.08), RNG.uniform(1.0, 1.2)))
        kit.beam(f"post_{sx}", o + Vector((sx * length / 2, 0, -0.05)), top, section=0.11)
        tops[sx] = top
    for z in (0.45, 0.9):  # rails follow the posts, so they slope
        a, b = tops[-1], tops[1]
        za, zb = z * a.z / 1.15, z * b.z / 1.15
        log(f"rail_{z}", Vector((a.x - 0.1, (a.y + o.y) / 2 + 0.07, o.z + za)), Vector((b.x + 0.1, (b.y + o.y) / 2 + 0.07, o.z + zb)), 0.055)


def well(origin=(0, 0, 0)):
    o = Vector(origin)
    for course in range(4):  # ring of stones
        n = 14
        for i in range(n):
            a = (i + course * 0.5) / n * math.tau
            r = 0.85
            kit.stone(f"wstone_{course}_{i}", o + Vector((math.cos(a) * r, math.sin(a) * r, 0.11 + course * 0.2)),
                     (0.4, 0.26, 0.19))
            bpy.data.objects[f"wstone_{course}_{i}"].rotation_euler.z = a + math.pi / 2
    water = bmesh.new()
    bmesh.ops.create_circle(water, cap_ends=True, segments=16, radius=0.75)
    kit.new_object("well_dark", water, "dark", location=o + Vector((0, 0, 0.55)))
    for sx in (-1, 1):
        kit.beam(f"wpost_{sx}", o + Vector((sx * 0.95, 0, 0)), o + Vector((sx * 0.95, 0, 2.1)), section=0.13)
    kit.beam("windlass", o + Vector((-1.05, 0, 1.45)), o + Vector((1.05, 0, 1.45)), section=0.08)
    for sy in (-1, 1):  # small pitched plank roof
        bm = kit.cube_bm((2.4, 0.9, 0.04), bevel=0.005)
        kit.new_object(f"wroof_{sy}", bm, "timber", grain_axis=1, location=o + Vector((0, sy * 0.38, 2.25)), rotation=(sy * 0.6, 0, 0))
    lathe("bucket", [(0.15, 0.0), (0.17, 0.25)], "timber", segments=12, location=o + Vector((0.3, 0.2, 1.0)))


def drying_rack(origin=(0, 0, 0), length=2.6):
    o = Vector(origin)
    for sx in (-1, 1):  # A-frames
        for sy in (-1, 1):
            kit.beam(f"leg_{sx}_{sy}", o + Vector((sx * length / 2, sy * 0.45, 0)), o + Vector((sx * length / 2, 0, 1.75)), section=0.06)
    log("ridge_pole", o + Vector((-length / 2 - 0.15, 0, 1.72)), o + Vector((length / 2 + 0.15, 0, 1.72)), 0.04)
    for k, (w, h) in enumerate(((0.9, 0.85), (0.7, 1.0))):  # hanging cloths
        bm = kit.cube_bm((w, 0.01, h), subdiv=3, jitter=0.02)
        kit.new_object(f"cloth_{k}", bm, "cloth", location=o + Vector((-0.6 + k * 1.1, 0.03, 1.72 - h / 2)))


def haystack(origin=(0, 0, 0)):
    o = Vector(origin)
    prof = [(1.25, 0.0), (1.3, 0.5), (1.15, 1.2), (0.8, 1.9), (0.35, 2.4), (0.05, 2.55)]
    lathe("hay", prof, "thatch", segments=20, location=o, wobble=0.08)
    kit.beam("hay_pole", o + Vector((0, 0, 2.2)), o + Vector((0, 0, 3.0)), section=0.06)


def bridge(origin=(0, 0, 0), length=9.0, width=2.2):
    """Plank footbridge along local Y: two log stringers on posts driven into each bank, planks laid across with
    gaps and slight unevenness, a rail on one side."""
    o = Vector(origin)
    deck = 0.45
    for sx in (-1, 1):
        log(f"stringer_{sx}", o + Vector((sx * width * 0.38, -length / 2, deck - 0.14)), o + Vector((sx * width * 0.38, length / 2, deck - 0.14)), 0.12)
        for sy in (-1, -0.33, 0.33, 1):  # posts: at each bank and two mid-stream piers
            y = sy * length * 0.42
            kit.beam(f"post_{sx}_{sy}", o + Vector((sx * width * 0.38, y, -1.4)), o + Vector((sx * width * 0.38, y, deck - 0.2)), section=0.14)
    y = -length / 2 + 0.1
    k = 0
    while y < length / 2 - 0.1:
        w = RNG.uniform(0.2, 0.28)
        bm = kit.cube_bm((width + RNG.uniform(-0.1, 0.1), w, 0.05), bevel=0.008, subdiv=1, jitter=0.006)
        kit.new_object(f"plank_{k}", bm, "timber", grain_axis=0,
                      location=o + Vector((RNG.uniform(-0.05, 0.05), y + w / 2, deck + RNG.uniform(-0.012, 0.012))),
                      rotation=(RNG.uniform(-0.02, 0.02), RNG.uniform(-0.02, 0.02), RNG.uniform(-0.015, 0.015)))
        y += w + RNG.uniform(0.015, 0.03)
        k += 1
    for sy in (-0.42, 0.0, 0.42):  # rail on the downstream side
        kit.beam(f"rail_post_{sy}", o + Vector((width / 2 - 0.05, sy * length, deck)), o + Vector((width / 2 - 0.05, sy * length, deck + 1.0)), section=0.09)
    log("rail", o + Vector((width / 2 - 0.05, -length * 0.45, deck + 0.95)), o + Vector((width / 2 - 0.05, length * 0.45, deck + 0.95)), 0.05)


def sack(origin=(0, 0, 0)):
    """Lumpy grain sack: a squashed, bulging lathe with a tied neck."""
    o = Vector(origin)
    prof = [(0.22, 0.0), (0.27, 0.08), (0.28, 0.25), (0.24, 0.42), (0.12, 0.52), (0.06, 0.56), (0.08, 0.62), (0.0, 0.64)]
    obj = lathe("sack", prof, "cloth", segments=20, location=o, wobble=0.12)
    obj.scale = (1.0, 0.8, 0.9)


def stall(origin=(0, 0, 0)):
    """Market stall: four posts, a plank counter on trestles, a sagging cloth awning, sacks and a crate under it."""
    o = Vector(origin)
    w, d = 2.6, 1.4
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = 2.3 if sy < 0 else 2.0  # awning slopes to the front
            kit.beam(f"post_{sx}_{sy}", o + Vector((sx * w / 2, sy * d / 2, 0)), o + Vector((sx * w / 2 + RNG.uniform(-0.03, 0.03), sy * d / 2, h)), section=0.1)
    for k in range(5):  # counter planks
        bm = kit.cube_bm((w - 0.2, 0.17, 0.04), bevel=0.005, subdiv=1, jitter=0.004)
        kit.new_object(f"counter_{k}", bm, "timber", grain_axis=0, location=o + Vector((0, -0.05 + (k - 2) * 0.18 + 0.35, 0.85)))
    for sx in (-1, 1):  # trestles
        kit.beam(f"trestle_{sx}", o + Vector((sx * (w / 2 - 0.3), 0.0, 0.0)), o + Vector((sx * (w / 2 - 0.3), 0.35, 0.83)), section=0.07)
    bm = kit.cube_bm((w + 0.3, d + 0.4, 0.02), subdiv=6)
    for vtx in bm.verts:  # sag between the posts and slope to the front
        x, y = vtx.co.x / (w + 0.3) + 0.5, vtx.co.y / (d + 0.4) + 0.5
        vtx.co.z -= 0.12 * math.sin(math.pi * x) * math.sin(math.pi * y) + (1.0 - y) * 0.3
    kit.new_object("awning", bm, "cloth", location=o + Vector((0, 0, 2.3)))
    sack(o + Vector((-0.7, -0.2, 0)))
    sack(o + Vector((-0.25, -0.35, 0)))
    crate(o + Vector((0.6, -0.15, 0)), size=0.55)


PROPS = {
    "barrel": barrel, "crate": crate, "woodpile": woodpile, "handcart": handcart, "wattle_fence": wattle_fence,
    "rail_fence": rail_fence, "well": well, "drying_rack": drying_rack, "haystack": haystack, "bridge": bridge,
    "sack": sack, "stall": stall,
}
