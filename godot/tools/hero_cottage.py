"""Hero cottage: one detailed 14th-c. English cottage built by script, to test whether asset detail closes the
"pre-rendered" gap. Individually placed irregular stones, bevelled and slightly warped timber frame with grain along
each beam, wattle-and-daub plaster with ground dirt, uneven layered thatch with ragged eaves and ridge cap, plank door
with iron strap hinges, shuttered windows, stacked-stone chimney, moss on stones and roof.

Run (Blender 5.2):
  Blender -b --factory-startup --python godot/tools/hero_cottage.py -- render <out_dir> [samples]
      -> <out_dir>/hero_cottage_close.png, hero_cottage_far.png (Cycles, CPU)
"""
import math
import pathlib
import random
import sys
import time

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, noise

TEX = pathlib.Path(__file__).resolve().parent.parent / "assets" / "textures"
SKY = TEX.parent / "sky.hdr"
RNG = random.Random(14)

W, D, H = 6.0, 4.5, 2.7   # wall footprint (x, y) and wall height above the footing
FOOT = 0.55               # stone footing height
PITCH_RISE = D * 0.62     # ridge height above the wall plate (~51° thatch)
TOP = FOOT + H

# metres covered by one texture tile, and which local axis the texture's V (grain / strands) follows
TILE = {"stone": 1.6, "plaster": 2.0, "timber": 1.4, "thatch": 1.8, "iron": 1.0, "dark": 1.0, "grass": 4.0}


# ---------------------------------------------------------------- geometry helpers

def new_object(name, bm, mat, grain_axis=2, location=(0, 0, 0), rotation=(0, 0, 0)):
    """Mesh object in its own local frame; box-projected UVs where texture V follows `grain_axis` (0=x,1=y,2=z)."""
    uv = bm.loops.layers.uv.new("UVMap")
    tile = TILE[mat]
    for f in bm.faces:
        n = f.normal
        axis = max(range(3), key=lambda i: abs(n[i]))
        others = [i for i in range(3) if i != axis]
        if grain_axis in others:
            v_axis = grain_axis
            u_axis = others[0] if others[1] == grain_axis else others[1]
        else:
            u_axis, v_axis = others
        for loop in f.loops:
            co = loop.vert.co
            loop[uv].uv = (co[u_axis] / tile, co[v_axis] / tile)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material(mat))
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    obj.rotation_euler = rotation
    bpy.context.scene.collection.objects.link(obj)
    for p in mesh.polygons:
        p.use_smooth = False
    return obj


def cube_bm(size, bevel=0.0, subdiv=0, jitter=0.0, seed=0):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Diagonal((*size, 1.0)))
    if subdiv:
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=subdiv, use_grid_fill=True)
    if jitter:
        for v in bm.verts:
            off = noise.noise_vector(v.co * 3.1 + Vector((seed, seed * 0.7, seed * 1.3)))
            v.co += off * jitter
    if bevel:
        bmesh.ops.bevel(bm, geom=bm.edges[:], offset=bevel, segments=1, affect="EDGES", profile=0.5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def beam(name, start, end, section=0.17, warp=0.012):
    """Timber between two points; local X runs along the beam so the grain follows it."""
    a, b = Vector(start), Vector(end)
    length = (b - a).length
    bm = cube_bm((length, section * RNG.uniform(0.92, 1.08), section * RNG.uniform(0.92, 1.08)), bevel=0.018, subdiv=2)
    bend = RNG.uniform(-warp, warp)
    for v in bm.verts:  # slight bow along the length
        t = v.co.x / length + 0.5
        v.co.z += bend * math.sin(t * math.pi)
    direction = (b - a).normalized()
    rot = direction.to_track_quat("X", "Z").to_euler()
    rot.x += RNG.uniform(-0.01, 0.01)
    return new_object(name, bm, "timber", grain_axis=0, location=(a + b) / 2, rotation=rot)


def stone(name, center, size, mat="stone"):
    bm = cube_bm(size, bevel=min(size) * 0.18, subdiv=1, jitter=min(size) * 0.18, seed=RNG.uniform(0, 100))
    return new_object(name, bm, mat, location=center, rotation=(RNG.uniform(-0.05, 0.05), RNG.uniform(-0.05, 0.05), RNG.uniform(-0.08, 0.08)))


# ---------------------------------------------------------------- building parts

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
    new_object("walls", bm, "plaster", location=(0, 0, FOOT + H / 2))
    bm = bmesh.new()
    tri = [(-D / 2 + 0.03, 0.0), (D / 2 - 0.03, 0.0), (0.0, PITCH_RISE)]
    for sx in (-1, 1):
        x = sx * (W / 2 - 0.03)
        verts = [bm.verts.new((x, y, TOP + z)) for y, z in tri]
        bm.faces.new(verts if sx > 0 else verts[::-1])
    new_object("gables", bm, "plaster", grain_axis=2)


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
        n = 4
        for i in range(1, n):
            x = -xs + i * 2 * xs / n
            if sy < 0 and i == 2:
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


def thatch():
    """Two slopes of layered thatch: courses step out every ~0.28 m, ragged eaves, plus a rounded ridge cap."""
    half = D / 2
    pitch = math.atan2(PITCH_RISE, half)
    over_eave, over_gable, thick = 0.55, 0.45, 0.4
    slope = math.hypot(half, PITCH_RISE) + over_eave
    length = W + 2 * over_gable
    nu, nv = 56, 26
    for side in (-1, 1):
        bm = bmesh.new()
        grid = []
        for j in range(nv + 1):
            row = []
            s = j / nv * slope                     # distance down-slope from the ridge
            course = (s / 0.28) % 1.0
            for i in range(nu + 1):
                x = -length / 2 + i / nu * length
                n = noise.noise(Vector((x * 1.7, s * 2.3, side * 5.0)))
                lift = thick * 0.55 + 0.07 * course + 0.04 * n
                droop = 0.0
                if j == nv:                         # ragged eave
                    droop = RNG.uniform(0.0, 0.12)
                if i in (0, nu):                    # rounded gable edge
                    lift *= 0.55
                row.append(bm.verts.new((x, s + droop * 0.3, lift - droop)))
            grid.append(row)
        for j in range(nv):
            for i in range(nu):
                bm.faces.new([grid[j][i], grid[j + 1][i], grid[j + 1][i + 1], grid[j][i + 1]])
        # thickness fringe at the eave and both gable edges, so the roof reads as a thick mat
        for i in range(nu):
            a, b = grid[nv][i], grid[nv][i + 1]
            a2 = bm.verts.new(a.co + Vector((0, -0.05, -thick)))
            b2 = bm.verts.new(b.co + Vector((0, -0.05, -thick)))
            bm.faces.new([a, a2, b2, b])
        for i in (0, nu):
            for j in range(nv):
                a, b = grid[j][i], grid[j + 1][i]
                a2 = bm.verts.new(a.co + Vector((0, 0, -thick)))
                b2 = bm.verts.new(b.co + Vector((0, 0, -thick)))
                bm.faces.new([a, b, b2, a2] if i == 0 else [a, a2, b2, b])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        for f in bm.faces:  # all normals should face up/out
            if f.normal.z < -0.2:
                f.normal_flip()
        # local frame: x along ridge, y down-slope, z out of the roof; strands follow y (down-slope)
        rot = Euler((-pitch, 0, 0 if side > 0 else math.pi))  # tilt local +y down-slope, then face it to its side
        new_object(f"thatch_{side}", bm, "thatch", grain_axis=1, location=(0, 0, TOP + PITCH_RISE + 0.05), rotation=rot)
    # ridge cap: a lumpy half-tube along the ridge
    bm = bmesh.new()
    seg, ring = 40, 10
    rows = []
    for i in range(seg + 1):
        x = -length / 2 - 0.05 + i / seg * (length + 0.1)
        row = []
        for k in range(ring + 1):
            a = math.pi * k / ring
            r = 0.32 + 0.04 * noise.noise(Vector((x * 2.0, k * 0.5, 9.0)))
            row.append(bm.verts.new((x, math.cos(a) * r * 1.2, math.sin(a) * r)))
        rows.append(row)
    for i in range(seg):
        for k in range(ring):
            bm.faces.new([rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]])
    new_object("ridge", bm, "thatch", grain_axis=0, location=(0, 0, TOP + PITCH_RISE + thick * 0.6))


def door_and_windows():
    front = -(D / 2 + 0.07)
    for k in range(5):  # door planks with small gaps
        x = -0.48 + k * 0.24
        bm = cube_bm((0.225, 0.06, 1.95), bevel=0.01, subdiv=1)
        new_object(f"door_plank_{k}", bm, "timber", grain_axis=2, location=(x, front, FOOT + 0.98))
    for z in (0.45, 1.5):  # iron strap hinges and a ledger behind each
        bm = cube_bm((0.95, 0.025, 0.06), bevel=0.005)
        new_object(f"hinge_{z}", bm, "iron", grain_axis=0, location=(-0.05, front - 0.04, FOOT + z))
    ring = bmesh.new()
    bmesh.ops.create_circle(ring, segments=12, radius=0.07)
    bmesh.ops.extrude_edge_only(ring, edges=ring.edges[:])
    new_object("door_ring", ring, "iron", location=(0.3, front - 0.06, FOOT + 1.0), rotation=(math.pi / 2, 0, 0))
    beam("door_head", (-0.65, front + 0.02, FOOT + 2.05), (0.65, front + 0.02, FOOT + 2.05), section=0.16)
    for side in (-1, 1):
        y = side * (D / 2 + 0.06)
        for x in (-W / 3.2, W / 3.2):
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


# ---------------------------------------------------------------- materials (Cycles)

_materials = {}


def material(slot):
    if slot in _materials:
        return _materials[slot]
    mat = bpy.data.materials.new(slot)
    nt = mat.node_tree
    nodes, links = nt.nodes, nt.links
    bsdf = nodes["Principled BSDF"]
    if slot in ("iron", "dark"):
        bsdf.inputs["Base Color"].default_value = (0.02, 0.019, 0.018, 1) if slot == "iron" else (0.005, 0.004, 0.003, 1)
        bsdf.inputs["Metallic"].default_value = 0.8 if slot == "iron" else 0.0
        bsdf.inputs["Roughness"].default_value = 0.55 if slot == "iron" else 0.9
        _materials[slot] = mat
        return mat
    folder = TEX / slot
    uvn = nodes.new("ShaderNodeUVMap")

    def tex(name, cs):
        t = nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(str(folder / f"{name}.jpg"), check_existing=True)
        t.image.colorspace_settings.name = cs
        links.new(uvn.outputs["UV"], t.inputs["Vector"])
        return t

    color = tex("albedo", "sRGB").outputs["Color"]
    geo = nodes.new("ShaderNodeNewGeometry")
    noise_t = nodes.new("ShaderNodeTexNoise")
    noise_t.inputs["Scale"].default_value = 1.6
    noise_t.inputs["Detail"].default_value = 6.0
    links.new(geo.outputs["Position"], noise_t.inputs["Vector"])

    def mix(a, b, factor, blend="MIX"):
        m = nodes.new("ShaderNodeMix")
        m.data_type, m.blend_type = "RGBA", blend
        links.new(factor, m.inputs["Factor"])
        if isinstance(a, tuple):
            m.inputs["A"].default_value = a
        else:
            links.new(a, m.inputs["A"])
        if isinstance(b, tuple):
            m.inputs["B"].default_value = b
        else:
            links.new(b, m.inputs["B"])
        return m.outputs["Result"]

    def ramp(value, lo, hi):
        r = nodes.new("ShaderNodeMapRange")
        r.inputs["From Min"].default_value, r.inputs["From Max"].default_value = lo, hi
        links.new(value, r.inputs["Value"])
        return r.outputs["Result"]

    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], sep.inputs["Vector"])
    nz = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Normal"], nz.inputs["Vector"])

    if slot == "grass":   # = village.gd TINT
        color = mix(color, (0.5, 0.66, 0.42, 1), _value(nodes, 1.0), "MULTIPLY")
    if slot == "thatch":  # Poly Haven thatch is grey: age it to straw brown, moss in damp patches
        color = mix(color, (0.78, 0.6, 0.38, 1), _value(nodes, 1.0), "MULTIPLY")
        moss = ramp(noise_t.outputs["Fac"], 0.62, 0.78)
        m4 = nodes.new("ShaderNodeMath")
        m4.operation = "MULTIPLY"
        links.new(moss, m4.inputs[0])
        m4.inputs[1].default_value = 0.6
        color = mix(color, (0.05, 0.07, 0.025, 1), m4.outputs["Value"])
    if slot == "stone":   # moss on upward faces
        up = ramp(nz.outputs["Z"], 0.5, 0.9)
        m2 = nodes.new("ShaderNodeMath")
        m2.operation = "MULTIPLY"
        links.new(up, m2.inputs[0])
        links.new(ramp(noise_t.outputs["Fac"], 0.45, 0.6), m2.inputs[1])
        color = mix(color, (0.07, 0.11, 0.04, 1), m2.outputs["Value"])
    if slot in ("plaster", "timber", "stone"):  # splash dirt near the ground
        dirt = ramp(sep.outputs["Z"], 1.1, 0.0)
        m3 = nodes.new("ShaderNodeMath")
        m3.operation = "MULTIPLY"
        links.new(dirt, m3.inputs[0])
        links.new(ramp(noise_t.outputs["Fac"], 0.3, 0.7), m3.inputs[1])
        color = mix(color, (0.16, 0.12, 0.08, 1), m3.outputs["Value"])
    ao = nodes.new("ShaderNodeAmbientOcclusion")  # grime in crevices
    ao.inputs["Distance"].default_value = 0.25
    links.new(color, ao.inputs["Color"])
    color = mix(color, (0.12, 0.09, 0.06, 1), ramp(ao.outputs["AO"], 0.9, 0.3))

    links.new(color, bsdf.inputs["Base Color"])
    links.new(tex("roughness", "Non-Color").outputs["Color"], bsdf.inputs["Roughness"])
    nmap = nodes.new("ShaderNodeNormalMap")
    links.new(tex("normal", "Non-Color").outputs["Color"], nmap.inputs["Color"])
    links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    _materials[slot] = mat
    return mat


def _value(nodes, v):
    n = nodes.new("ShaderNodeValue")
    n.outputs[0].default_value = v
    return n.outputs[0]


# ---------------------------------------------------------------- scene + render

def build():
    footing()
    walls()
    frame()
    thatch()
    door_and_windows()
    chimney()


def stage():
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=30.0)
    new_object("ground", bm, "grass")
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 4.5
    sun.data.angle = math.radians(1.5)
    sun.rotation_euler = Euler((math.radians(50), 0, math.radians(-35)))
    bpy.context.scene.collection.objects.link(sun)
    world = bpy.data.worlds.new("sky")
    bpy.context.scene.world = world
    env = world.node_tree.nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(str(SKY))
    world.node_tree.links.new(env.outputs["Color"], world.node_tree.nodes["Background"].inputs["Color"])
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8


def camera(name, pitch_deg, yaw_deg, fov_deg, framed_width, focus=(0, 0, 2.0)):
    cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(fov_deg)
    dist = framed_width / (2 * math.tan(math.radians(fov_deg) / 2) * 16 / 9)
    pitch, yaw = math.radians(pitch_deg), math.radians(yaw_deg)
    # same convention as scripts/camera_zoom.gd: looking down `pitch`, from the south-east at `yaw`
    offset = Vector((math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch))) * dist
    cam.location = Vector(focus) + offset
    cam.rotation_euler = (-offset).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.collection.objects.link(cam)
    return cam


def render(out_dir, samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"  # Metal GPU rendering aborts in background mode on this Mac
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    shots = {
        "hero_cottage_far": camera("far", 45, 45, 12, 40),   # the game's far iso camera (40 m across)
        "hero_cottage_close": camera("close", 32, 35, 35, 12),
    }
    for name, cam in shots.items():
        scene.camera = cam
        scene.render.filepath = str(pathlib.Path(out_dir) / f"{name}.png")
        t = time.time()
        bpy.ops.render.render(write_still=True)
        print(f"hero_cottage: {name} in {time.time() - t:.1f}s")


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build()
    tris = sum(len(p.vertices) - 2 for o in bpy.data.objects if o.type == "MESH" for p in o.data.polygons)
    print(f"hero_cottage: {tris} triangles, {len(bpy.data.objects)} objects")
    stage()
    if args[0] == "render":
        render(args[1], int(args[2]) if len(args) > 2 else 64)


main()
