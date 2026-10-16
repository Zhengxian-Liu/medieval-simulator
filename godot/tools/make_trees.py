"""Generates fir trees procedurally in Blender and exports them as GLB into godot/assets/models/trees/.

No downloads: trunk is a tapered cone, foliage is tiers of drooping branch "cards" (4 triangles each),
coloured through a vertex colour layer (dark near the trunk, lighter at the tips).

Run: /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python godot/tools/make_trees.py
"""
import math
import pathlib
import random

import bmesh
import bpy

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "models" / "trees"
VARIANTS = 4

BARK = (0.16, 0.10, 0.06)
NEEDLE_BASE = (0.035, 0.085, 0.045)
NEEDLE_TIP = (0.10, 0.19, 0.08)


def lerp3(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def jitter(c, rng, amount=0.15):
    k = 1.0 + rng.uniform(-amount, amount)
    return tuple(min(1.0, x * k) for x in c) + (1.0,)


def add_face(bm, col, verts, colors):
    face = bm.faces.new([bm.verts.new(v) for v in verts])
    for loop, c in zip(face.loops, colors):
        loop[col] = c


def trunk(bm, col, rng, height, radius, sides=10):
    ring = lambda z, r: [(math.cos(a) * r, math.sin(a) * r, z) for a in (i / sides * math.tau for i in range(sides))]
    bottom, top = ring(0.0, radius), ring(height, radius * 0.08)
    c = jitter(BARK, rng, 0.1)
    for i in range(sides):
        j = (i + 1) % sides
        add_face(bm, col, [bottom[i], bottom[j], top[j], top[i]], [c, c, c, c])


def branch(bm, col, rng, origin, yaw, length, droop):
    """Diamond card with a raised centre so it reads from above and from the side."""
    w = length * rng.uniform(0.3, 0.4)
    ca, sa = math.cos(yaw), math.sin(yaw)
    to_world = lambda x, y, z: (origin[0] + x * ca - y * sa, origin[1] + x * sa + y * ca, origin[2] + z)
    b = to_world(0, 0, 0)
    l = to_world(0.45 * length, w, -droop * 0.35)
    r = to_world(0.45 * length, -w, -droop * 0.35)
    c = to_world(0.5 * length, 0, -droop * 0.4 + 0.12)
    t = to_world(length, 0, -droop)
    base = jitter(NEEDLE_BASE, rng)
    mid = jitter(lerp3(NEEDLE_BASE, NEEDLE_TIP, 0.5), rng)
    tip = jitter(NEEDLE_TIP, rng)
    for verts, cols in (([b, l, c], [base, mid, mid]), ([b, c, r], [base, mid, mid]),
                        ([l, t, c], [mid, tip, mid]), ([c, t, r], [mid, tip, mid])):
        add_face(bm, col, verts, cols)


def fir(seed):
    rng = random.Random(seed)
    height = rng.uniform(6.5, 9.5)
    crown_radius = height * rng.uniform(0.26, 0.32)
    bm = bmesh.new()
    col = bm.loops.layers.float_color.new("Col")
    trunk(bm, col, rng, height, height * 0.035)
    z = height * rng.uniform(0.15, 0.22)
    while z < height * 0.97:
        frac = (z - height * 0.15) / (height * 0.85)  # 0 at the lowest tier, 1 at the top
        count = rng.randint(9, 12)
        offset = rng.uniform(0, math.tau)
        for i in range(count):
            yaw = offset + i / count * math.tau + rng.uniform(-0.2, 0.2)
            length = max(0.25, crown_radius * (1.0 - frac) ** 0.9 * rng.uniform(0.8, 1.1))
            branch(bm, col, rng, (0.0, 0.0, z), yaw, length, droop=length * rng.uniform(0.25, 0.45))
        z += rng.uniform(0.22, 0.32)
    mesh = bpy.data.meshes.new(f"fir_{seed}")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(f"fir_{seed}", mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for seed in range(VARIANTS):
        obj = fir(seed)
        for o in bpy.context.scene.objects:
            o.select_set(o == obj)
        path = OUT / f"fir_{seed}.glb"
        bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True, export_yup=True)
        print(f"wrote {path} ({len(obj.data.polygons)} faces)")
        bpy.data.objects.remove(obj)


main()
