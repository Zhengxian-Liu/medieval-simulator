"""Builds /Game/Maps/TestVillage from engine basic shapes: houses, river, torch, daylight.

Placeholder geometry only; swap meshes for Megascans/Tripo assets later. Rerunning rebuilds the map from scratch.
Run: see CLAUDE.md ("Rebuild test scene").
"""
import unreal

MAP = "/Game/Maps/TestVillage"
MAT_DIR = "/Game/Materials/Placeholder"
BASE_MAT = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.AssetToolsHelpers.get_asset_tools()


def material(name, rgb):
    path = f"{MAT_DIR}/{name}"
    mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(
        name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, BASE_MAT)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*rgb, 1.0))
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def block(mesh, mat, loc, size_cm, yaw=0.0, roll=0.0):
    """Mesh scaled so its bounds are size_cm (basic shapes are 100 cm)."""
    a = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), unreal.Rotator(roll=roll, pitch=0.0, yaw=yaw))
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_material(0, mat)
    a.set_actor_scale3d(unreal.Vector(*(s / 100.0 for s in size_cm)))
    return a


def house(x, y, yaw, wall, roof):
    w, d, h = 600, 450, 320
    block(CUBE, STONE, (x, y, 40), (w + 20, d + 20, 80), yaw)
    block(CUBE, wall, (x, y, 80 + h / 2), (w, d, h), yaw)
    # Two roof slabs leaning on the ridge.
    for side in (-1, 1):
        off = unreal.Vector(0, side * d * 0.27, 0).rotate_angle_axis(yaw, unreal.Vector(0, 0, 1))
        block(CUBE, roof, (x + off.x, y + off.y, 80 + h + 110), (w + 60, d * 0.62, 25), yaw, roll=side * 35)


if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    if not levels.load_level(MAP):
        raise RuntimeError("load_level failed for " + MAP)
    actors.destroy_actors(actors.get_all_level_actors())
elif not levels.new_level(MAP):
    raise RuntimeError("new_level failed for " + MAP)

GRASS = material("MI_Grass", (0.09, 0.13, 0.05))
STONE = material("MI_Stone", (0.25, 0.24, 0.22))
PLASTER = material("MI_Plaster", (0.55, 0.50, 0.42))
TIMBER = material("MI_Timber", (0.18, 0.11, 0.06))
THATCH = material("MI_Thatch", (0.33, 0.25, 0.12))
WATER = material("MI_Water", (0.02, 0.06, 0.07))
IRON = material("MI_Iron", (0.05, 0.05, 0.05))

block(CUBE, GRASS, (0, 0, -50), (12000, 12000, 100))
block(CUBE, WATER, (0, 1400, -5), (12000, 700, 20), yaw=8)
block(CUBE, TIMBER, (300, 1440, 30), (250, 900, 20), yaw=98)  # footbridge

house(-900, -600, 10, PLASTER, THATCH)
house(600, -900, -15, TIMBER, THATCH)
house(-300, -1900, 80, PLASTER, STONE)
house(1500, 300, 40, PLASTER, THATCH)

block(CYLINDER, IRON, (150, -150, 75), (12, 12, 150))
torch = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(150, -150, 165))
tl = torch.point_light_component
tl.set_editor_property("use_temperature", True)
tl.set_editor_property("temperature", 2200.0)
tl.set_editor_property("intensity", 100.0)  # candela; a real torch is ~100 cd
tl.set_editor_property("attenuation_radius", 900.0)

sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(roll=0.0, pitch=-42.0, yaw=-130.0))
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sun.light_component.set_editor_property("intensity", 10.0)
actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
sky.light_component.set_editor_property("real_time_capture", True)
actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))

pp = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
pp.set_editor_property("unbound", True)
s = pp.settings
s.set_editor_property("override_ambient_occlusion_intensity", True)
s.set_editor_property("ambient_occlusion_intensity", 0.6)
pp.set_editor_property("settings", s)

actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(150, -350, 100))

if not levels.save_current_level():
    raise RuntimeError("save failed for " + MAP)
unreal.log("build_test_scene: saved " + MAP)
