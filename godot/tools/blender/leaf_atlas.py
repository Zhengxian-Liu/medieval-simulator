"""Leaf-cluster atlas: renders 6 clumps of 15-30 oak leaves (ambientCG LeafSet016 cards, CC0) on a twig in Cycles and
writes a 3x2 atlas the foliage cards sample (Fable rounds 9-11: one flat unshaded leaf per card cannot make lit
clusters with shadowed gaps; the cluster card itself must carry its light and shade).

Light: a hard key from 45 deg above and to the front plus a dimmer white sky, over a dark ground: each clump carries its
own lit tops, self-shadowed gaps and dark undersides (Fable r12: an evenly lit atlas left the crowns flat mid-green).
Godot still lights the card as a whole by its normal. The atlas's mean opacity goes to atlas.json for foliage.py.

  Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- atlas
-> godot/assets/textures/leaf_clusters/{color,opacity,normalgl}.jpg (1020x1024, cells 340x512)
"""
import math
import pathlib
import random
import subprocess

import bpy
from mathutils import Matrix, Vector

GODOT = pathlib.Path(__file__).resolve().parents[2]
SRC = GODOT / "assets" / "textures" / "leaves_oak"
OUT = GODOT / "assets" / "textures" / "leaf_clusters"
COLS, ROWS = 3, 2
CELL_W, CELL_H = 0.6, 0.9   # metres of clump per cell (card aspect 2:3)
PX_W, PX_H = 340, 512
SRC_COLS, SRC_ROWS = 3, 2   # leaves_oak atlas layout


def leaf_material():
    m = bpy.data.materials.new("leaf")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    col = nt.nodes.new("ShaderNodeTexImage")
    col.image = bpy.data.images.load(str(SRC / "color.jpg"))
    op = nt.nodes.new("ShaderNodeTexImage")
    op.image = bpy.data.images.load(str(SRC / "opacity.jpg"))
    op.image.colorspace_settings.name = "Non-Color"
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.7
    nt.links.new(col.outputs["Color"], bsdf.inputs["Base Color"])
    clear = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(op.outputs["Color"], mix.inputs["Fac"])
    nt.links.new(clear.outputs[0], mix.inputs[1])
    nt.links.new(bsdf.outputs[0], mix.inputs[2])
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    return m


def flat_material(name, rgb):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1.0)
    return m


def clump(rng, centre, leaf_mat, twig_mat):
    """15-30 leaves around a twig inside one cell, facing the camera (-Y) with random tilt; denser in the middle."""
    verts, faces, uvs = [], [], []
    for _ in range(30):
        p = centre + Vector((rng.gauss(0, CELL_W * 0.13), rng.uniform(-0.12, 0.12), rng.gauss(0, CELL_H * 0.12)))
        p.x = max(min(p.x, centre.x + CELL_W * 0.22), centre.x - CELL_W * 0.22)  # leaves (up to 0.32 m) stay in the cell
        p.z = max(min(p.z, centre.z + CELL_H * 0.28), centre.z - CELL_H * 0.3)
        size = rng.uniform(0.15, 0.22)  # real oak leaves ~8-12 cm long at ~1.3x card scale (Fable r12: 1.5x too big)  # a dense clump: leaves overlap, the card reads as a mass with gaps
        n = Vector((rng.uniform(-0.6, 0.6), -1.0, rng.uniform(-0.4, 0.8))).normalized()
        q = n.to_track_quat("Z", "Y") @ Matrix.Rotation(rng.uniform(0, math.tau), 3, "Z").to_quaternion()
        w, h = size * 0.75, size
        cell = rng.randrange(SRC_COLS * SRC_ROWS)
        u0, v0 = (cell % SRC_COLS) / SRC_COLS, (cell // SRC_COLS) / SRC_ROWS
        du, dv = 1 / SRC_COLS, 1 / SRC_ROWS
        base = len(verts)
        for c in (Vector((-w / 2, -h * 0.1, 0)), Vector((w / 2, -h * 0.1, 0)), Vector((w / 2, h * 0.9, 0)), Vector((-w / 2, h * 0.9, 0))):
            verts.append(p + q @ c)
        faces.append((base, base + 1, base + 2, base + 3))
        uvs += [(u0, v0), (u0 + du, v0), (u0 + du, v0 + dv), (u0, v0 + dv)]
    me = bpy.data.meshes.new("clump")
    me.from_pydata(verts, [], faces)
    uv = me.uv_layers.new()
    for i, loop in enumerate(me.loops):
        uv.data[i].uv = uvs[loop.vertex_index]
    obj = bpy.data.objects.new("clump", me)
    obj.data.materials.append(leaf_mat)
    bpy.context.scene.collection.objects.link(obj)
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.008, depth=CELL_H * 0.45,
                                        location=centre + Vector((0, 0.05, -CELL_H * 0.22)))
    bpy.context.active_object.data.materials.append(twig_mat)


def build():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"  # Metal GPU rendering aborts in background mode on this Mac
    scene.cycles.samples = 128
    scene.cycles.use_denoising = True
    scene.render.film_transparent = True
    scene.render.resolution_x, scene.render.resolution_y = PX_W * COLS, PX_H * ROWS
    scene.view_settings.view_transform = "Standard"  # straight albedo x light into sRGB, no filmic curve
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Strength"].default_value = 0.45
    sun = bpy.data.objects.new("key", bpy.data.lights.new("key", "SUN"))
    sun.data.energy = 2.0  # x cos / pi on a Lambert leaf: lit ~1.1x, shaded ~0.45x albedo
    sun.data.angle = math.radians(2.0)
    sun.rotation_euler = (math.radians(45), 0, math.radians(20))  # from above and slightly in front of the camera
    scene.collection.objects.link(sun)
    bg.inputs["Color"].default_value = (1, 1, 1, 1)
    scene.world = world
    # dark ground below (hidden from the camera): undersides darken, as under a real crown
    bpy.ops.mesh.primitive_plane_add(size=50, location=(0, 0, -CELL_H * ROWS))
    ground = bpy.context.active_object
    ground.data.materials.append(flat_material("ground", (0.08, 0.08, 0.06)))
    ground.visible_camera = False
    leaf_mat, twig_mat = leaf_material(), flat_material("twig", (0.12, 0.09, 0.06))
    rng = random.Random(16)
    for r in range(ROWS):
        for c in range(COLS):
            centre = Vector(((c - (COLS - 1) / 2) * CELL_W, 0, ((ROWS - 1) / 2 - r) * CELL_H))
            clump(rng, centre, leaf_mat, twig_mat)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(CELL_W * COLS, CELL_H * ROWS)
    cam.data.sensor_fit = "AUTO"
    cam.location = (0, -5, 0)
    cam.rotation_euler = (math.pi / 2, 0, 0)
    scene.collection.objects.link(cam)
    scene.camera = cam
    OUT.mkdir(parents=True, exist_ok=True)
    rgba = OUT / "_render.png"
    scene.render.filepath = str(rgba)
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    bpy.ops.render.render(write_still=True)
    # colour bled into the transparent parts (no dark fringes once mipmapped), opacity as its own map, flat normal
    subprocess.run(["magick", str(rgba), "(", "+clone", "-channel", "RGBA", "-blur", "0x12", ")", "+swap", "-compose", "over",
                    "-composite", "-alpha", "off", "-quality", "92", str(OUT / "color.jpg")], check=True)
    subprocess.run(["magick", str(rgba), "-alpha", "extract", "-quality", "95", str(OUT / "opacity.jpg")], check=True)
    subprocess.run(["magick", "-size", f"{PX_W * COLS}x{PX_H * ROWS}", "xc:rgb(128,128,255)", str(OUT / "normalgl.jpg")], check=True)
    rgba.unlink()
    opacity = float(subprocess.run(["magick", str(OUT / "opacity.jpg"), "-format", "%[fx:mean]", "info:"],
                                   capture_output=True, text=True, check=True).stdout)
    (OUT / "atlas.json").write_text(f'{{"mean_opacity": {opacity:.3f}}}\n')
    print(f"leaf_atlas: {OUT}")
