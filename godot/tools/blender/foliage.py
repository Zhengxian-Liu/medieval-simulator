"""Broadleaf oaks and bushes: branching bark limbs plus thousands of single-leaf cards from the ambientCG oak
atlas. Vertex colour R = canopy depth (dark interior), normals point out of the crown so it lights as one mass.
Surfaces "bark" and "leaves" are shaded in Godot (shaders/leaves.gdshader, art/look.json materials.leaves)."""
import math
import random

import bmesh
import bpy
from mathutils import Matrix, Vector

ATLAS_COLS, ATLAS_ROWS = 3, 2

# name: (seed, height, crown radius, trunk radius, leaves, leaf size)
VARIANTS = {
    "oak_0": (1, 8.5, 3.6, 0.28, 14000, 0.22),  # many small leaves read as a dense crown, few big ones as confetti
    "oak_1": (2, 7.0, 3.0, 0.24, 11500, 0.21),
    "oak_2": (3, 9.5, 4.0, 0.32, 16000, 0.23),
    # bushes are seen close in the mid view: real leaf size (~10 cm), more of them (0.17 m cards read as big confetti,
    # Fable round 6)
    "bush_0": (11, 1.3, 1.1, 0.0, 5000, 0.10),
    "bush_1": (12, 1.0, 0.9, 0.0, 3600, 0.095),
    "bush_2": (13, 1.6, 1.3, 0.0, 6400, 0.105),
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


def leaf(mesh, rng, center, size, normal_hint, shade=1.0):
    """One leaf card: random atlas cell, random orientation biased to face outward/upward from the crown."""
    cell = rng.randrange(ATLAS_COLS * ATLAS_ROWS)
    cu, cv = cell % ATLAS_COLS, cell // ATLAS_COLS
    u0, v0 = cu / ATLAS_COLS, cv / ATLAS_ROWS
    du, dv = 1 / ATLAS_COLS, 1 / ATLAS_ROWS
    n = (normal_hint + Vector((rng.uniform(-0.7, 0.7), rng.uniform(-0.7, 0.7), rng.uniform(-0.2, 0.9)))).normalized()
    q = n.to_track_quat("Z", "Y") @ Matrix.Rotation(rng.uniform(0, math.tau), 3, "Z").to_quaternion()
    w, h = size * 0.75, size
    corners = [Vector((-w / 2, -h * 0.1, 0)), Vector((w / 2, -h * 0.1, 0)), Vector((w / 2, h * 0.9, 0)), Vector((-w / 2, h * 0.9, 0))]
    uvs = [(u0, v0), (u0 + du, v0), (u0 + du, v0 + dv), (u0, v0 + dv)]
    mesh.face([center + q @ c for c in corners], uvs, 1, shade, normal_hint)


def inner(mesh, center, radius, rings=6, segs=10):
    """Low-poly sphere using the leaf material's darkest atlas corner (UV 0,0 region is opaque leaf body)."""
    verts = []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        verts.append([center + Vector((math.sin(phi) * math.cos(t), math.sin(phi) * math.sin(t), math.cos(phi) * 0.8)) * radius
                      for t in (i / segs * math.tau for i in range(segs + 1))])
    uv = (0.5 / ATLAS_COLS, 0.5 / ATLAS_ROWS)  # centre of the first leaf: opaque green
    for r in range(rings):
        for i in range(segs):
            quad = [verts[r][i], verts[r + 1][i], verts[r + 1][i + 1], verts[r][i + 1]]
            mesh.face(quad, [uv] * 4, 1, 0.12, (sum(quad, Vector()) / 4 - center).normalized())  # deep shade


def build(name, seed, height, crown, trunk_r, leaves, leaf_size):
    rng = random.Random(seed)
    mesh = Mesh()
    crown_center = Vector((0, 0, height * (0.62 if trunk_r else 0.45)))
    tips = []
    if trunk_r:
        top = Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), height * 0.55))
        limb(mesh, rng, Vector((0, 0, -0.2)), top, trunk_r * 1.25, trunk_r * 0.7, sides=9)
        for i in range(rng.randint(5, 7)):  # main branches, then twigs toward the crown surface
            ang = i / 6 * math.tau + rng.uniform(-0.4, 0.4)
            start = top + Vector((0, 0, rng.uniform(-0.6, 0.6)))
            end = crown_center + Vector((math.cos(ang) * crown * 0.7, math.sin(ang) * crown * 0.7, rng.uniform(-0.6, 1.4)))
            limb(mesh, rng, start, end, trunk_r * 0.55, trunk_r * 0.18)
            for _ in range(4):
                twig_end = end + Vector((rng.uniform(-1.2, 1.2), rng.uniform(-1.2, 1.2), rng.uniform(-0.4, 1.0)))
                limb(mesh, rng, end, twig_end, trunk_r * 0.18, 0.02, sides=5)
                tips.append(twig_end)
    if trunk_r:  # dark inner crown volume behind the leaves, so gaps show shade instead of sky (reads as one dense canopy)
        for c in [crown_center] + [crown_center + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.5))) * crown * 0.35 for _ in range(4)]:
            inner(mesh, c, crown * rng.uniform(0.28, 0.34))  # well inside the leaf shell: never visible as a facet
    # leaf clumps: points inside a lumpy ellipsoid, concentrated near its surface so the crown reads as masses
    lobes = [crown_center + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.4, 0.6))) * crown * 0.45
             for _ in range(7)] + tips
    for _ in range(leaves):
        c = rng.choice(lobes)
        d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 0.8))).normalized()
        r = crown * rng.uniform(0.4, 0.6) * (rng.random() ** 0.15)  # mostly near the lobe surface: a closed canopy
        p = c + d * r
        p.z = max(p.z, 0.15 if not trunk_r else height * 0.32)
        out = (p - crown_center)
        depth = min(1.0, out.length / (crown * 0.75))
        leaf(mesh, rng, p, leaf_size * rng.uniform(0.8, 1.2), out.normalized(), 0.25 + 0.75 * depth ** 1.5)
    me = bpy.data.meshes.new(name)
    mesh.to_mesh(me)
    for slot in ("bark", "leaves"):
        me.materials.append(bpy.data.materials.get(slot) or bpy.data.materials.new(slot))
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    return obj
