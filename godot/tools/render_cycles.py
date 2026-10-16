"""Renders the Godot village offline with Blender Cycles from the same far-iso camera: the "pre-rendered" reference.

1. godot --headless --path godot --quit-after 2000 -- --export-gltf=/abs/village.glb
2. Blender -b --factory-startup --python godot/tools/render_cycles.py -- /abs/village.glb /abs/out.png [samples] [GPU|CPU]

Godot's materials are triplanar (no UVs), so every slot is rebuilt here by name with box-projected Poly Haven
textures at the same tiling as scripts/village.gd. Imported Poly Haven models keep their own materials.
"""
import math
import pathlib
import re
import sys
import time

import bpy

ASSETS = pathlib.Path(__file__).resolve().parent.parent / "assets"
TILES = {"grass": 0.25, "riverbed": 0.4, "stone": 0.6, "plaster": 0.5, "timber": 0.6, "thatch": 0.5, "rock": 0.8}  # = village.gd TEXTURED
TINT = {"grass": (0.5, 0.66, 0.42, 1.0)}  # = village.gd TINT
FLAT = {"dark": (0.008, 0.006, 0.004), "iron": (0.016, 0.016, 0.016), "cloth": (0.19, 0.025, 0.018)}


def textured(slot):
    mat = bpy.data.materials.new(f"cy_{slot}")
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    pos = nt.nodes.new("ShaderNodeNewGeometry")
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (TILES[slot],) * 3
    nt.links.new(pos.outputs["Position"], mapping.inputs["Vector"])

    def tex(name, colorspace):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(str(ASSETS / "textures" / slot / f"{name}.jpg"), check_existing=True)
        t.image.colorspace_settings.name = colorspace
        t.projection = "BOX"
        t.projection_blend = 0.25
        nt.links.new(mapping.outputs["Vector"], t.inputs["Vector"])
        return t

    albedo = tex("albedo", "sRGB")
    if slot in TINT:
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        nt.links.new(albedo.outputs["Color"], mix.inputs["A"])
        mix.inputs["B"].default_value = TINT[slot]
        nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
    else:
        nt.links.new(albedo.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(tex("roughness", "Non-Color").outputs["Color"], bsdf.inputs["Roughness"])
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tex("normal", "Non-Color").outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def flat(rgb, roughness=0.8):
    mat = bpy.data.materials.new("cy_flat")
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    return mat


def foliage():
    """Godot treats the tree vertex colours as sRGB; the attribute is linear here, so convert."""
    mat = bpy.data.materials.new("cy_foliage")
    nt = mat.node_tree
    attr = nt.nodes.new("ShaderNodeVertexColor")
    gamma = nt.nodes.new("ShaderNodeGamma")
    gamma.inputs["Gamma"].default_value = 2.2
    nt.links.new(attr.outputs["Color"], gamma.inputs["Color"])
    bsdf = nt.nodes["Principled BSDF"]
    nt.links.new(gamma.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.85
    return mat


def water():
    mat = flat((0.01, 0.05, 0.06), roughness=0.04)
    mat.node_tree.nodes["Principled BSDF"].inputs["IOR"].default_value = 1.33
    return mat


def retexture():
    built = {}
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        if obj.name.startswith("Water"):
            obj.data.materials.clear()
            obj.data.materials.append(built.setdefault("water", water()))
            continue
        for i, m in enumerate(obj.data.materials):
            if m is None:
                continue
            slot = re.sub(r"\.\d+$", "", m.name)
            if slot == "stone_rock":
                slot = "rock"
            if slot in TILES:
                obj.data.materials[i] = built.setdefault(slot, textured(slot))
            elif slot in FLAT:
                obj.data.materials[i] = built.setdefault(slot, flat(FLAT[slot]))
            elif slot == "foliage":
                obj.data.materials[i] = built.setdefault(slot, foliage())


def lights_and_world():
    for obj in bpy.data.objects:
        if obj.type == "LIGHT":
            if obj.data.type == "SUN":
                obj.data.energy = 4.5
                obj.data.angle = math.radians(1.5)  # soft penumbra, like a real sun
            else:
                obj.data.energy = 150.0
                obj.data.color = (1.0, 0.6, 0.25)
    world = bpy.data.worlds.new("sky")
    bpy.context.scene.world = world
    nt = world.node_tree
    env = nt.nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(str(ASSETS / "sky.hdr"))
    nt.links.new(env.outputs["Color"], nt.nodes["Background"].inputs["Color"])
    nt.nodes["Background"].inputs["Strength"].default_value = 0.8


def main():
    glb, out = sys.argv[sys.argv.index("--") + 1:][:2]
    extra = sys.argv[sys.argv.index("--") + 3:]
    samples = int(extra[0]) if extra else 128
    device = extra[1] if len(extra) > 1 else "GPU"
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=glb)
    retexture()
    lights_and_world()

    scene = bpy.context.scene
    cam = next(o for o in bpy.data.objects if o.type == "CAMERA")
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(12.0)  # = CameraZoom FAR_FOV (vertical)
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.device = device
    if device == "GPU":
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices:
            d.use = True
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    scene.render.filepath = out
    t = time.time()
    bpy.ops.render.render(write_still=True)
    print(f"render_cycles: {out} in {time.time() - t:.1f}s at {samples} samples")


main()
