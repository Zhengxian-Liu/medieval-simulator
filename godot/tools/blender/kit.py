"""Shared geometry and material kit for every scripted asset (buildings, props, bridge, fences...).

new_object() gives each part box-projected UVs whose V follows the part's grain (beams along their length, planks
along the board, thatch down the slope), a named material slot, and flat shading. Slot names are the contract with
Godot: art/look.json materials.weathered.<slot> decides how each slot is shaded in game. The Cycles materials here
are only for offline previews (preview.py).
"""
import math
import pathlib
import random

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, noise

TEX = pathlib.Path(__file__).resolve().parents[2] / "assets" / "textures"
SKY = TEX.parent / "sky.hdr"
RNG = random.Random(14)  # reseeded per asset by build_asset.py so every asset is deterministic

# metres covered by one texture tile per slot (box-projected UVs)
TILE = {"stone": 1.6, "plaster": 2.0, "timber": 1.4, "thatch": 1.8, "iron": 1.0, "dark": 1.0, "cloth": 1.0, "grass": 4.0}

_materials = {}


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
    return new_object(name, bm, mat, grain_axis=2, location=location)


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
    return new_object(name, bm, "timber", grain_axis=0, location=(a + b) / 2,
                         rotation=direction.to_track_quat("X", "Z").to_euler())


def material(slot):
    if slot in _materials:
        return _materials[slot]
    mat = bpy.data.materials.new(slot)
    nt = mat.node_tree
    nodes, links = nt.nodes, nt.links
    bsdf = nodes["Principled BSDF"]
    if slot in ("iron", "dark", "cloth"):
        bsdf.inputs["Base Color"].default_value = {"iron": (0.02, 0.019, 0.018, 1), "dark": (0.005, 0.004, 0.003, 1),
                                                   "cloth": (0.45, 0.4, 0.32, 1)}[slot]
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
