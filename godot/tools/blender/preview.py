"""Offline Cycles preview of one scripted building (diagnostic for the pre-rendered target, not used in game)."""
import math
import pathlib
import time

import bpy
import bmesh
from mathutils import Euler, Vector

from kit import SKY, new_object


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
