"""Join an asset's parts, bake, and export a GLB the way Godot expects it.

- materials are exported by name only (export_image_format NONE): textures are applied in Godot per slot. Exporting
  images duplicated every texture into every GLB (1.8 MB barrels) - never re-enable it.
- the active colour layer (baked AO + convexity) is exported as COLOR_0.
"""
import pathlib

import bpy

from bake import bake_ao


def join_all(name):
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    return obj


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def export_glb(obj, path, vertex_colors=True):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True, export_yup=True,
                              export_vertex_color="ACTIVE" if vertex_colors else "NONE", export_image_format="NONE")


def bake_and_export(name, path):
    """Standard path for hard-surface assets: join, bake AO + edges, export. Returns the triangle count."""
    obj = join_all(name)
    bake_ao(obj)
    export_glb(obj, path)
    return triangles(obj)
