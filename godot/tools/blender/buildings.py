"""Buildings: a spec per building (footprint, wall height, infill slot, chimney, door, windows) and the parts that
build it: irregular stone footing, daub walls with subdivided gables, bevelled timber frame, layered thatch,
plank door with iron hinges, shuttered windows, stacked-stone chimney.

Add a building: add one line to VARIANTS, then `build_asset.py -- building <name>`.
"""
import math

import bmesh
from mathutils import Vector

from kit import RNG, beam, cube_bm, new_object, stone
from roofs import thatch_roof

FOOT = 0.55               # stone footing height

# name: wall footprint (x, y), wall height above the footing, infill material, chimney, wide barn door, windows
VARIANTS = {
    "cottage": dict(w=6.0, d=4.5, h=2.7, wall="plaster", chimney=True, big_door=False, windows=True),
    "longhouse": dict(w=9.5, d=5.0, h=2.9, wall="plaster", chimney=True, big_door=False, windows=True),
    "barn": dict(w=7.5, d=5.5, h=3.3, wall="timber", chimney=False, big_door=True, windows=False),
}
W = D = H = PITCH_RISE = TOP = 0.0
WALL, CHIMNEY, BIG_DOOR, WINDOWS = "plaster", True, False, True


def set_variant(name):
    global W, D, H, PITCH_RISE, TOP, WALL, CHIMNEY, BIG_DOOR, WINDOWS
    v = VARIANTS[name]
    W, D, H = v["w"], v["d"], v["h"]
    PITCH_RISE = D * 0.62  # ridge height above the wall plate (~51° thatch)
    TOP = FOOT + H
    WALL, CHIMNEY, BIG_DOOR, WINDOWS = v["wall"], v["chimney"], v["big_door"], v["windows"]


def footing():
    """Two courses of irregular stones around the perimeter, plus a rubble core."""
    core = cube_bm((W + 0.1, D + 0.1, FOOT - 0.05))
    new_object("footing_core", core, "stone", location=(0, 0, (FOOT - 0.05) / 2))
    course_h = FOOT / 2
    for course in range(2):
        z = course_h * (course + 0.5)
        for side, (length, fixed, along_x) in enumerate([(W + 0.4, -(D / 2 + 0.12), True), (W + 0.4, D / 2 + 0.12, True),
                                                          (D + 0.4, -(W / 2 + 0.12), False), (D + 0.4, W / 2 + 0.12, False)]):
            pos = -length / 2 + (0.2 if course else 0.0)
            while pos < length / 2 - 0.15:
                sl = RNG.uniform(0.32, 0.6)
                size = (sl, RNG.uniform(0.22, 0.32), course_h * RNG.uniform(0.85, 1.05))
                c = pos + sl / 2
                loc = (c, fixed, z) if along_x else (fixed, c, z)
                if not along_x:
                    size = (size[1], size[0], size[2])
                stone(f"stone_{course}_{side}_{pos:.2f}", loc, size)
                pos += sl + RNG.uniform(0.01, 0.04)


def walls():
    """Plaster infill (daub) set back slightly behind the frame, plus closed gables."""
    bm = cube_bm((W - 0.06, D - 0.06, H), subdiv=6, jitter=0.012)
    new_object("walls", bm, WALL, grain_axis=2, location=(0, 0, FOOT + H / 2))
    bm = bmesh.new()
    tri = [(-D / 2 + 0.03, 0.0), (D / 2 - 0.03, 0.0), (0.0, PITCH_RISE)]
    for sx in (-1, 1):
        x = sx * (W / 2 - 0.03)
        verts = [bm.verts.new((x, y, TOP + z)) for y, z in tri]
        bm.faces.new(verts if sx > 0 else verts[::-1])
    # baked AO is per vertex: a bare 3-vertex triangle (all corners under the roof) bakes solid black
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=8, use_grid_fill=True)
    new_object("gables", bm, WALL, grain_axis=2)


def frame():
    p = 0.06  # beams stand proud of the plaster
    xs, ys = W / 2 + p - 0.08, D / 2 + p - 0.08
    for sx in (-1, 1):
        for sy in (-1, 1):
            beam(f"post_{sx}_{sy}", (sx * xs, sy * ys, FOOT), (sx * xs, sy * ys, TOP), section=0.2)
    for sy in (-1, 1):
        y = sy * ys
        beam(f"sill_{sy}", (-xs - 0.1, y, FOOT + 0.08), (xs + 0.1, y, FOOT + 0.08), section=0.2)
        beam(f"plate_{sy}", (-xs - 0.25, y, TOP - 0.08), (xs + 0.25, y, TOP - 0.08), section=0.2)
        beam(f"rail_{sy}", (-xs, y, FOOT + H * 0.5), (xs, y, FOOT + H * 0.5), section=0.13)
        n = max(3, round(W / 1.5))
        for i in range(1, n):
            x = -xs + i * 2 * xs / n
            if sy < 0 and abs(x) < (1.4 if BIG_DOOR else 0.75):
                continue  # door goes here on the front
            beam(f"stud_{sy}_{i}", (x, y, FOOT + 0.08), (x, y, TOP - 0.08), section=0.15)
        for sx in (-1, 1):  # curved-looking corner braces
            beam(f"brace_{sy}_{sx}", (sx * xs, y, FOOT + H * 0.5), (sx * (xs - 1.0), y, TOP - 0.1), section=0.12)
    for sx in (-1, 1):
        x = sx * xs
        beam(f"gsill_{sx}", (x, -ys, FOOT + 0.08), (x, ys, FOOT + 0.08), section=0.2)
        beam(f"gplate_{sx}", (x, -ys, TOP - 0.08), (x, ys, TOP - 0.08), section=0.2)
        beam(f"kingpost_{sx}", (x, 0, TOP), (x, 0, TOP + PITCH_RISE * 0.85), section=0.16)
        for sy in (-1, 1):  # principal rafters visible in the gable
            beam(f"rafter_{sx}_{sy}", (x, sy * ys, TOP), (x, 0, TOP + PITCH_RISE * 0.95), section=0.16)


def door_and_windows():
    front = -(D / 2 + 0.07)
    planks, door_h = (10, 2.6) if BIG_DOOR else (5, 1.95)
    for k in range(planks):  # door planks with small gaps
        x = -0.24 * (planks - 1) / 2 + k * 0.24
        bm = cube_bm((0.225, 0.06, door_h), bevel=0.01, subdiv=1)
        new_object(f"door_plank_{k}", bm, "timber", grain_axis=2, location=(x, front, FOOT + door_h / 2 + 0.03))
    hinge_w = 0.24 * planks * 0.8
    for z in (0.45, door_h - 0.45):  # iron strap hinges
        for half in ((-1, 1) if BIG_DOOR else (0,)):
            bm = cube_bm((hinge_w / (2 if BIG_DOOR else 1), 0.025, 0.06), bevel=0.005)
            new_object(f"hinge_{z}_{half}", bm, "iron", grain_axis=0, location=(half * hinge_w / 4, front - 0.04, FOOT + z))
    ring = bmesh.new()
    bmesh.ops.create_circle(ring, segments=12, radius=0.07)
    bmesh.ops.extrude_edge_only(ring, edges=ring.edges[:])
    new_object("door_ring", ring, "iron", location=(0.3, front - 0.06, FOOT + 1.0), rotation=(math.pi / 2, 0, 0))
    head = 0.12 * planks + 0.05
    beam("door_head", (-head, front + 0.02, FOOT + door_h + 0.1), (head, front + 0.02, FOOT + door_h + 0.1), section=0.16)
    if not WINDOWS:
        return
    for side in (-1, 1):
        y = side * (D / 2 + 0.06)
        for x in ((-W / 3.2, W / 3.2) if W < 8 else (-W / 2.6, -W / 6.5, W / 6.5, W / 2.6)):
            if side < 0 and abs(x) < 0.9:
                continue
            z = FOOT + H * 0.6
            bm = cube_bm((0.65, 0.05, 0.55))
            new_object(f"win_{side}_{x:.1f}", bm, "dark", location=(x, y - side * 0.02, z))
            beam(f"win_head_{side}_{x:.1f}", (x - 0.45, y, z + 0.33), (x + 0.45, y, z + 0.33), section=0.11)
            beam(f"win_sill_{side}_{x:.1f}", (x - 0.45, y, z - 0.33), (x + 0.45, y, z - 0.33), section=0.11)
            for s in (-1, 1):  # open plank shutters
                bm = cube_bm((0.34, 0.04, 0.6), bevel=0.006, subdiv=1)
                new_object(f"shutter_{side}_{x:.1f}_{s}", bm, "timber", grain_axis=2,
                           location=(x + s * 0.55, y + side * 0.06, z), rotation=(0, 0, s * side * 0.35))


def chimney():
    x, y = W / 2 - 0.85, 0.75
    z = 0.0
    top = TOP + PITCH_RISE + 0.9
    while z < top:
        h = RNG.uniform(0.18, 0.26)
        for dx in (-0.2, 0.2):
            for dy in (-0.2, 0.2):
                stone(f"chim_{z:.2f}_{dx}_{dy}", (x + dx + RNG.uniform(-0.03, 0.03), y + dy + RNG.uniform(-0.03, 0.03), z + h / 2),
                      (RNG.uniform(0.36, 0.44), RNG.uniform(0.36, 0.44), h * 0.95))
        z += h
    bm = cube_bm((0.95, 0.95, 0.12), bevel=0.02, jitter=0.02)
    new_object("chim_cap", bm, "stone", location=(x, y, top + 0.06))


def build(name):
    set_variant(name)
    footing()
    walls()
    frame()
    thatch_roof(W, D, TOP, PITCH_RISE)
    door_and_windows()
    if CHIMNEY:
        chimney()
