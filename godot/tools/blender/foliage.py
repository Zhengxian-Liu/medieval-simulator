"""Broadleaf oaks and bushes: branching bark limbs plus leaf-cluster cards (each card one baked clump of 24-30 oak
leaves with its own occlusion, tools/blender/leaf_atlas.py -> assets/textures/leaf_clusters). Vertex colour R = canopy
depth (dark interior), normals point out of the crown so it lights as one mass.
Surfaces "bark" and "leaves" are shaded in Godot (shaders/leaves.gdshader, art/look.json materials.leaves)."""
import json
import math
import pathlib
import random

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

ATLAS_COLS, ATLAS_ROWS = 3, 2

# name: (seed, height, crown radius, trunk radius, card cover, card height m). Single-leaf cards (11-16k per tree) could
# not make lit clusters with shadowed gaps (Fable rounds 8-11). The card count follows from the lobes' surface: cover x
# lobe area / (card area x atlas mean opacity); 60-120 cards per tree left the crown see-through (round 12 A/Bs)
ATLAS_JSON = pathlib.Path(__file__).resolve().parents[2] / "assets" / "textures" / "leaf_clusters" / "atlas.json"
VARIANTS = {
    "oak_0": (1, 8.5, 3.6, 0.28, 1.3, 1.7),
    "oak_1": (2, 7.0, 3.0, 0.24, 1.3, 1.5),
    "oak_2": (3, 9.5, 4.0, 0.32, 1.3, 1.85),
    "bush_0": (11, 1.3, 1.1, 0.0, 1.3, 0.75),
    "bush_1": (12, 1.0, 0.9, 0.0, 1.3, 0.65),
    "bush_2": (13, 1.6, 1.3, 0.0, 1.3, 0.85),
}


class Mesh:
    """Faces with UVs, a vertex colour "shade" (canopy depth: 0 deep inside .. 1 outer surface) and an optional
    normal override, so a crown of flat cards lights like one rounded mass (art review 2026-10-16)."""

    def __init__(self):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.col = self.bm.loops.layers.float_color.new("Col")
        self.normals = []  # per face: override vector or None (keep the geometric normal)

    def face(self, verts, uvs, mat, shade=1.0, normal=None):
        f = self.bm.faces.new([self.bm.verts.new(v) for v in verts])
        f.material_index = mat
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
            loop[self.col] = (shade, shade, shade, 1.0)
        self.normals.append(normal)

    def to_mesh(self, me):
        self.bm.to_mesh(me)
        self.bm.free()
        loop_normals = []
        for poly, n in zip(me.polygons, self.normals):
            loop_normals += [tuple(n) if n is not None else tuple(poly.normal)] * poly.loop_total
        me.normals_split_custom_set(loop_normals)


def limb(mesh, rng, a, b, r0, r1, sides=7):
    """Tapered bark cylinder from a to b; bark UV wraps around (u) and runs along the limb (v)."""
    axis = (b - a)
    length = axis.length
    q = axis.normalized().to_track_quat("Z", "Y")
    rings = []
    for k, (p, r) in enumerate(((a, r0), (b, r1))):
        ring = []
        for i in range(sides + 1):
            ang = i / sides * math.tau
            off = q @ Vector((math.cos(ang) * r, math.sin(ang) * r, 0))
            ring.append((p + off, (i / sides, k * length / 1.5)))
        rings.append(ring)
    for i in range(sides):
        a0, a1 = rings[0][i], rings[0][i + 1]
        b0, b1 = rings[1][i], rings[1][i + 1]
        mesh.face([a0[0], a1[0], b1[0], b0[0]], [a0[1], a1[1], b1[1], b0[1]], 0)


def card(mesh, rng, center, size, normal_hint, shade=1.0):
    """One leaf-cluster card: random atlas cell (twig at the bottom), facing out of the crown with jitter, kept upright."""
    cell = rng.randrange(ATLAS_COLS * ATLAS_ROWS)
    cu, cr = cell % ATLAS_COLS, cell // ATLAS_COLS
    u0, v0 = cu / ATLAS_COLS, 1 - (cr + 1) / ATLAS_ROWS  # atlas row 0 is the top of the image
    du, dv = 1 / ATLAS_COLS, 1 / ATLAS_ROWS
    n = (normal_hint + Vector((rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), rng.uniform(-0.1, 0.5)))).normalized()
    q = n.to_track_quat("Z", "Y") @ Matrix.Rotation(rng.uniform(-0.5, 0.5), 3, "Z").to_quaternion()
    w, h = size * 2 / 3, size
    corners = [Vector((-w / 2, -h / 2, 0)), Vector((w / 2, -h / 2, 0)), Vector((w / 2, h / 2, 0)), Vector((-w / 2, h / 2, 0))]
    uvs = [(u0, v0), (u0 + du, v0), (u0 + du, v0 + dv), (u0, v0 + dv)]
    mesh.face([center + q @ c for c in corners], uvs, 1, shade, normal_hint)


def inner(mesh, center, radius, rings=6, segs=10):
    """Lumpy low-poly volume using the leaf material's opaque atlas cell (UV 0,0 region is opaque leaf body): fills the
    gaps between leaf cards. Lumps and a top-lit / dark-underside shade so it reads as foliage mass, not a smooth
    sphere (Fable round 8)."""
    verts = []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        row = []
        for t in (i / segs * math.tau for i in range(segs + 1)):
            d = Vector((math.sin(phi) * math.cos(t), math.sin(phi) * math.sin(t), math.cos(phi) * 0.8))
            row.append(center + d * radius * (1.0 + 0.35 * noise.noise(d * 2.2 + center)))
        verts.append(row)
    uv = (0.5 / ATLAS_COLS, 0.5 / ATLAS_ROWS)  # centre of the first leaf: opaque green
    for r in range(rings):
        for i in range(segs):
            quad = [verts[r][i], verts[r + 1][i], verts[r + 1][i + 1], verts[r][i + 1]]
            n = (sum(quad, Vector()) / 4 - center).normalized()
            mesh.face(quad, [uv] * 4, 1, 0.15 + 0.35 * (n.z * 0.5 + 0.5), n)  # lit top, dark underside


def build(name, seed, height, crown, trunk_r, cover, leaf_size):
    rng = random.Random(seed)
    mesh = Mesh()
    crown_center = Vector((0, 0, height * (0.62 if trunk_r else 0.45)))
    lobes = []
    lobe_r = crown * (0.4 if trunk_r else 0.45)
    if trunk_r:
        top = Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), height * 0.55))
        limb(mesh, rng, Vector((0, 0, -0.2)), top, trunk_r * 1.25, trunk_r * 0.7, sides=9)
        for i in range(rng.randint(5, 7)):  # main branches, then twigs toward the crown surface
            ang = i / 6 * math.tau + rng.uniform(-0.4, 0.4)
            start = top + Vector((0, 0, rng.uniform(-0.6, 0.6)))
            end = crown_center + Vector((math.cos(ang) * crown * 0.7, math.sin(ang) * crown * 0.7, rng.uniform(-0.6, 1.4)))
            limb(mesh, rng, start, end, trunk_r * 0.55, trunk_r * 0.18)
            # each branch carries its own leaf clusters (The Guild's oaks: 6-10 lit clusters with shadowed gaps, Fable
            # rounds 8-9); twigs stay inside them so no bare skeleton shows (round 10 A/Bs)
            for c in (end, end.lerp(crown_center, 0.35) + Vector((0, 0, lobe_r * 0.9))):
                lobes.append(c)
                for _ in range(2):
                    twig_end = c + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.8))) * lobe_r * 0.5
                    limb(mesh, rng, end, twig_end, trunk_r * 0.18, 0.02, sides=5)
    else:  # bushes: lobes around the centre
        lobes = [crown_center + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.4, 0.6))) * crown * 0.45
                 for _ in range(7)]
        inner(mesh, crown_center, lobe_r * 0.5)
    # inner volume in every lobe, just under its leaf shell: gaps show leafy shade, not ground or sky (round 7)
    for c in lobes:
        inner(mesh, c, lobe_r * rng.uniform(0.5, 0.58))  # well behind the cards: fills gaps, never seen as a facet
    opacity = json.loads(ATLAS_JSON.read_text())["mean_opacity"]  # mean opacity of an atlas cell (leaf_atlas.py)
    cards = int(cover * len(lobes) * 4 * math.pi * lobe_r ** 2 / (leaf_size * leaf_size * 2 / 3 * opacity))
    for _ in range(cards):  # cluster cards on each lobe surface
        c = rng.choice(lobes)
        d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0.3, 0.8))).normalized()
        p = c + d * lobe_r * rng.uniform(0.7, 0.95)  # card centre just inside the lobe surface: its clump reaches out
        p.z = max(p.z, 0.25 if not trunk_r else height * 0.38)
        out = (p - crown_center)
        depth = min(1.0, out.length / (crown * 0.75))
        up = d if d.z < 0.3 else (d + Vector((0, 0, 0.8))).normalized()  # upper cards face the sky: the crown's lit cap
        card(mesh, rng, p, leaf_size * rng.uniform(0.85, 1.15), up, 0.35 + 0.65 * depth ** 1.5)
    me = bpy.data.meshes.new(name)
    mesh.to_mesh(me)
    for slot in ("bark", "leaves"):
        me.materials.append(bpy.data.materials.get(slot) or bpy.data.materials.new(slot))
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj
