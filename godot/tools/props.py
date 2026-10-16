"""Yard clutter kit at hero-cottage detail, built with the same helpers and material slots as tools/hero_cottage.py:
barrel, crate, woodpile, handcart, wattle fence, split-rail fence, well, drying rack, haystack.

Run: Blender -b --factory-startup --python godot/tools/props.py -- <out_dir>   (one GLB per prop)
"""
import math
import pathlib
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import hero_cottage as hc  # noqa: E402  (helpers: cube_bm, new_object, beam, stone, material slots)

RNG = hc.RNG


def lathe(name, profile, mat, segments=16, cap=True, location=(0, 0, 0), wobble=0.0):
    """Surface of revolution around local Z from (radius, z) pairs; texture grain runs up Z."""
    bm = bmesh.new()
    rings = []
    for r, z in profile:
        ring = []
        for i in range(segments):
            a = i / segments * math.tau
            rr = r * (1 + wobble * noise.noise(Vector((math.cos(a) * 3, math.sin(a) * 3, z * 4))))
            ring.append(bm.verts.new((math.cos(a) * rr, math.sin(a) * rr, z)))
        rings.append(ring)
    for j in range(len(rings) - 1):
        for i in range(segments):
            k = (i + 1) % segments
            bm.faces.new([rings[j][i], rings[j][k], rings[j + 1][k], rings[j + 1][i]])
    if cap:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return hc.new_object(name, bm, mat, grain_axis=2, location=location)


def log(name, a, b, radius):
    """Split log: an irregular 7-sided cylinder between two points, grain along its length."""
    a, b = Vector(a), Vector(b)
    length = (b - a).length
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=7, radius1=radius, radius2=radius * 0.95, depth=length)
    for v in bm.verts:
        v.co.x += noise.noise(v.co * 7.0) * radius * 0.25
        v.co.y += noise.noise(v.co * 7.0 + Vector((3, 1, 2))) * radius * 0.25
    bm.transform(Matrix.Rotation(math.pi / 2, 4, "Y"))  # length along local X
    direction = (b - a).normalized()
    return hc.new_object(name, bm, "timber", grain_axis=0, location=(a + b) / 2,
                         rotation=direction.to_track_quat("X", "Z").to_euler())


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
            bm = hc.cube_bm(dims, bevel=0.004)
            hc.new_object(f"slat_{k}_{sx}_{sy}", bm, "timber", grain_axis=0 if sx == 0 else 1,
                          location=o + Vector((sx * s / 2, sy * s / 2, z + s / 8)))
    for sx in (-1, 1):  # corner battens and lid
        for sy in (-1, 1):
            bm = hc.cube_bm((0.05, 0.05, s), bevel=0.006)
            hc.new_object(f"batten_{sx}_{sy}", bm, "timber", grain_axis=2, location=o + Vector((sx * (s / 2 - 0.02), sy * (s / 2 - 0.02), s / 2)))
    bm = hc.cube_bm((s + 0.02, s + 0.02, 0.03), bevel=0.005)
    hc.new_object("lid", bm, "timber", grain_axis=0, location=o + Vector((0, 0, s + 0.015)))


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
        hc.beam(f"stake_{sx}", o + Vector((sx * (length / 2 + 0.05), 0, 0)), o + Vector((sx * (length / 2 + 0.05), 0, 1.05)), section=0.08)


def wheel(name, center, radius=0.48):
    c = Vector(center)
    rim = lathe(f"{name}_rim", [(radius, -0.04), (radius, 0.04), (radius - 0.06, 0.04), (radius - 0.06, -0.04), (radius, -0.04)],
                "timber", segments=24, cap=False)
    rim.rotation_euler = (math.pi / 2, 0, 0)
    rim.location = c
    for k in range(8):
        a = k / 8 * math.tau
        tip = c + Vector((math.cos(a) * (radius - 0.05), 0, math.sin(a) * (radius - 0.05)))
        hc.beam(f"{name}_spoke_{k}", c, tip, section=0.035, warp=0.0)
    hub = lathe(f"{name}_hub", [(0.07, -0.09), (0.08, 0.0), (0.07, 0.09)], "timber", segments=10)
    hub.rotation_euler = (math.pi / 2, 0, 0)
    hub.location = c


def handcart(origin=(0, 0, 0)):
    o = Vector(origin)
    bed_z = 0.62
    for k in range(6):  # bed planks
        bm = hc.cube_bm((1.5, 0.15, 0.04), bevel=0.005)
        hc.new_object(f"bed_{k}", bm, "timber", grain_axis=0, location=o + Vector((0, -0.4 + k * 0.16, bed_z)))
    for sy in (-1, 1):  # side boards and handles
        bm = hc.cube_bm((1.5, 0.04, 0.28), bevel=0.005)
        hc.new_object(f"side_{sy}", bm, "timber", grain_axis=0, location=o + Vector((0, sy * 0.47, bed_z + 0.16)))
        hc.beam(f"handle_{sy}", o + Vector((0.7, sy * 0.42, bed_z - 0.03)), o + Vector((1.75, sy * 0.36, 0.32)), section=0.06)
        wheel(f"wheel_{sy}", o + Vector((-0.15, sy * 0.58, 0.48)))
    hc.beam("axle", o + Vector((-0.15, -0.62, 0.48)), o + Vector((-0.15, 0.62, 0.48)), section=0.06)
    hc.beam("prop_leg", o + Vector((0.75, 0, bed_z - 0.04)), o + Vector((0.75, 0, 0.0)), section=0.06)


def wattle_fence(origin=(0, 0, 0), length=2.4):
    o = Vector(origin)
    stakes = int(length / 0.4) + 1
    for i in range(stakes):
        x = -length / 2 + i * length / (stakes - 1)
        hc.beam(f"stake_{i}", o + Vector((x, 0, -0.05)), o + Vector((x + RNG.uniform(-0.03, 0.03), RNG.uniform(-0.02, 0.02), 1.05)), section=0.05)
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
        hc.new_object(f"rod_{row}", bm, "timber", grain_axis=0, location=o)


def rail_fence(origin=(0, 0, 0), length=3.0):
    o = Vector(origin)
    for sx in (-1, 1):
        hc.beam(f"post_{sx}", o + Vector((sx * length / 2, 0, -0.05)), o + Vector((sx * length / 2, 0, 1.15)), section=0.11)
    for z in (0.45, 0.9):
        log(f"rail_{z}", o + Vector((-length / 2 - 0.1, 0.07, z + RNG.uniform(-0.04, 0.04))),
            o + Vector((length / 2 + 0.1, 0.07, z + RNG.uniform(-0.04, 0.04))), 0.055)


def well(origin=(0, 0, 0)):
    o = Vector(origin)
    for course in range(4):  # ring of stones
        n = 14
        for i in range(n):
            a = (i + course * 0.5) / n * math.tau
            r = 0.85
            hc.stone(f"wstone_{course}_{i}", o + Vector((math.cos(a) * r, math.sin(a) * r, 0.11 + course * 0.2)),
                     (0.4, 0.26, 0.19))
            bpy.data.objects[f"wstone_{course}_{i}"].rotation_euler.z = a + math.pi / 2
    water = bmesh.new()
    bmesh.ops.create_circle(water, cap_ends=True, segments=16, radius=0.75)
    hc.new_object("well_dark", water, "dark", location=o + Vector((0, 0, 0.55)))
    for sx in (-1, 1):
        hc.beam(f"wpost_{sx}", o + Vector((sx * 0.95, 0, 0)), o + Vector((sx * 0.95, 0, 2.1)), section=0.13)
    hc.beam("windlass", o + Vector((-1.05, 0, 1.45)), o + Vector((1.05, 0, 1.45)), section=0.08)
    for sy in (-1, 1):  # small pitched plank roof
        bm = hc.cube_bm((2.4, 0.9, 0.04), bevel=0.005)
        hc.new_object(f"wroof_{sy}", bm, "timber", grain_axis=1, location=o + Vector((0, sy * 0.38, 2.25)), rotation=(sy * 0.6, 0, 0))
    lathe("bucket", [(0.15, 0.0), (0.17, 0.25)], "timber", segments=12, location=o + Vector((0.3, 0.2, 1.0)))


def drying_rack(origin=(0, 0, 0), length=2.6):
    o = Vector(origin)
    for sx in (-1, 1):  # A-frames
        for sy in (-1, 1):
            hc.beam(f"leg_{sx}_{sy}", o + Vector((sx * length / 2, sy * 0.45, 0)), o + Vector((sx * length / 2, 0, 1.75)), section=0.06)
    log("ridge_pole", o + Vector((-length / 2 - 0.15, 0, 1.72)), o + Vector((length / 2 + 0.15, 0, 1.72)), 0.04)
    for k, (w, h) in enumerate(((0.9, 0.85), (0.7, 1.0))):  # hanging cloths
        bm = hc.cube_bm((w, 0.01, h), subdiv=3, jitter=0.02)
        hc.new_object(f"cloth_{k}", bm, "cloth", location=o + Vector((-0.6 + k * 1.1, 0.03, 1.72 - h / 2)))


def haystack(origin=(0, 0, 0)):
    o = Vector(origin)
    prof = [(1.25, 0.0), (1.3, 0.5), (1.15, 1.2), (0.8, 1.9), (0.35, 2.4), (0.05, 2.55)]
    lathe("hay", prof, "thatch", segments=20, location=o, wobble=0.08)
    hc.beam("hay_pole", o + Vector((0, 0, 2.2)), o + Vector((0, 0, 3.0)), section=0.06)


def bridge(origin=(0, 0, 0), length=9.0, width=2.2):
    """Plank footbridge along local Y: two log stringers on posts driven into each bank, planks laid across with
    gaps and slight unevenness, a rail on one side."""
    o = Vector(origin)
    deck = 0.45
    for sx in (-1, 1):
        log(f"stringer_{sx}", o + Vector((sx * width * 0.38, -length / 2, deck - 0.14)), o + Vector((sx * width * 0.38, length / 2, deck - 0.14)), 0.12)
        for sy in (-1, -0.33, 0.33, 1):  # posts: at each bank and two mid-stream piers
            y = sy * length * 0.42
            hc.beam(f"post_{sx}_{sy}", o + Vector((sx * width * 0.38, y, -1.4)), o + Vector((sx * width * 0.38, y, deck - 0.2)), section=0.14)
    y = -length / 2 + 0.1
    k = 0
    while y < length / 2 - 0.1:
        w = RNG.uniform(0.2, 0.28)
        bm = hc.cube_bm((width + RNG.uniform(-0.1, 0.1), w, 0.05), bevel=0.008, subdiv=1, jitter=0.006)
        hc.new_object(f"plank_{k}", bm, "timber", grain_axis=0,
                      location=o + Vector((RNG.uniform(-0.05, 0.05), y + w / 2, deck + RNG.uniform(-0.012, 0.012))),
                      rotation=(RNG.uniform(-0.02, 0.02), RNG.uniform(-0.02, 0.02), RNG.uniform(-0.015, 0.015)))
        y += w + RNG.uniform(0.015, 0.03)
        k += 1
    for sy in (-0.42, 0.0, 0.42):  # rail on the downstream side
        hc.beam(f"rail_post_{sy}", o + Vector((width / 2 - 0.05, sy * length, deck)), o + Vector((width / 2 - 0.05, sy * length, deck + 1.0)), section=0.09)
    log("rail", o + Vector((width / 2 - 0.05, -length * 0.45, deck + 0.95)), o + Vector((width / 2 - 0.05, length * 0.45, deck + 0.95)), 0.05)


PROPS = {
    "barrel": barrel, "crate": crate, "woodpile": woodpile, "handcart": handcart, "wattle_fence": wattle_fence,
    "rail_fence": rail_fence, "well": well, "drying_rack": drying_rack, "haystack": haystack, "bridge": bridge,
}


def main():
    out = pathlib.Path(sys.argv[sys.argv.index("--") + 1])
    out.mkdir(parents=True, exist_ok=True)
    for name, make in PROPS.items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        hc._materials.clear()
        RNG.seed(hash(name) & 0xFFFF)
        make()
        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        obj = bpy.context.view_layer.objects.active
        obj.name = name
        hc.bake_ao(obj)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        path = out / f"{name}.glb"
        for o in bpy.context.scene.objects:
            o.select_set(o == obj)
        bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True, export_yup=True,
                                  export_vertex_color="ACTIVE", export_image_format="NONE")  # textures are applied in Godot by slot name
        print(f"props: {name} {tris} triangles -> {path}")


main()
