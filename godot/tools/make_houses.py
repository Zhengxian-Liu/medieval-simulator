"""Generates medieval village houses in Blender and exports them as GLB into godot/assets/models/houses/.

Built from boxes and prisms: stone plinth, walls, timber frame (posts, sills, plates, braces), closed gables,
thick thatch roof with overhang and ridge cap, door, framed windows with shutters, optional chimney.
Faces carry material *names* (stone, plaster, timber, thatch, dark); Godot maps each name to its textured
material (scripts/village.gd), so no UVs are needed here.

Run: /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python godot/tools/make_houses.py
"""
import math
import pathlib

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "models" / "houses"
MATERIALS = ["stone", "plaster", "timber", "thatch", "dark"]

# name: (length x, depth y, wall height, wall material, timber frame, chimney, big door)
VARIANTS = {
    "cottage_a": (6.0, 4.5, 2.8, "plaster", True, True, False),
    "cottage_b": (7.5, 4.8, 3.0, "plaster", True, False, False),
    "barn": (6.5, 5.0, 3.2, "timber", False, False, True),
}

PLINTH = 0.5
BEAM = 0.18  # timber section, metres


class Builder:
    def __init__(self):
        self.bm = bmesh.new()

    def box(self, mat, loc, size, rot=(0.0, 0.0, 0.0)):
        before = set(self.bm.faces)
        m = Matrix.LocRotScale(Vector(loc), Euler(rot), Vector(size))
        bmesh.ops.create_cube(self.bm, size=1.0, matrix=m)
        for f in set(self.bm.faces) - before:
            f.material_index = MATERIALS.index(mat)

    def prism_x(self, mat, x0, x1, base_z, half_depth, rise):
        """Triangular prism along X: closes both gables between wall top and ridge."""
        tri = [(-half_depth, base_z), (half_depth, base_z), (0.0, base_z + rise)]
        a = [self.bm.verts.new((x0, y, z)) for y, z in tri]
        b = [self.bm.verts.new((x1, y, z)) for y, z in tri]
        faces = [self.bm.faces.new(a[::-1]), self.bm.faces.new(b)]
        for i in range(3):
            j = (i + 1) % 3
            faces.append(self.bm.faces.new([a[i], a[j], b[j], b[i]]))
        for f in faces:
            f.material_index = MATERIALS.index(mat)

    def to_object(self, name):
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        mesh = bpy.data.meshes.new(name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for mname in MATERIALS:
            mat = bpy.data.materials.get(mname) or bpy.data.materials.new(mname)
            mesh.materials.append(mat)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def timber_frame(b, w, d, h):
    top = PLINTH + h
    proud = 0.04  # beams sit slightly outside the wall plane
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box("timber", (sx * (w / 2 + proud - BEAM / 2), sy * (d / 2 + proud - BEAM / 2), PLINTH + h / 2), (BEAM, BEAM, h))
    for sy in (-1, 1):  # long walls: sill, plate, studs, corner braces
        y = sy * (d / 2 + proud - BEAM / 2)
        b.box("timber", (0, y, PLINTH + BEAM / 2), (w + 2 * proud, BEAM, BEAM))
        b.box("timber", (0, y, top - BEAM / 2), (w + 2 * proud, BEAM, BEAM))
        n = max(2, round(w / 1.6))
        for i in range(1, n):
            b.box("timber", (-w / 2 + i * w / n, y, PLINTH + h / 2), (BEAM * 0.8, BEAM, h))
        brace = math.hypot(w / n, h) * 0.95
        ang = math.atan2(h, w / n)
        for sx in (-1, 1):
            cx = sx * (w / 2 - w / n / 2)
            b.box("timber", (cx, y, PLINTH + h / 2), (brace, BEAM * 0.9, BEAM * 0.7), (0, sx * ang, 0))
    for sx in (-1, 1):  # gable walls: sill and plate
        x = sx * (w / 2 + proud - BEAM / 2)
        b.box("timber", (x, 0, PLINTH + BEAM / 2), (BEAM, d + 2 * proud, BEAM))
        b.box("timber", (x, 0, top - BEAM / 2), (BEAM, d + 2 * proud, BEAM))


def roof(b, w, d, top, rise, chimney):
    half = d / 2
    pitch = math.atan2(rise, half)
    slope = math.hypot(half, rise)
    overhang, thick = 0.55, 0.32
    width = slope + overhang
    ridge = Vector((0, 0, top + rise))
    for side in (-1, 1):  # side -1 is the front (-Y)
        down = Vector((0, side * math.cos(pitch), -math.sin(pitch)))
        normal = Vector((0, side * math.sin(pitch), math.cos(pitch)))
        center = ridge + down * (width / 2) + normal * (thick / 2)
        b.box("thatch", center, (w + 0.9, width, thick), (-side * pitch, 0, 0))
    b.box("thatch", ridge + Vector((0, 0, thick * 0.9)), (w + 0.9, 0.5, 0.3), (math.pi / 4, 0, 0))
    if chimney:
        x = w / 2 - 0.9
        b.box("stone", (x, 0.6, top + rise * 0.6), (0.75, 0.75, rise * 1.5 + 0.8))


def openings(b, w, d, h, big_door):
    front = -d / 2 - 0.05
    door_w, door_h = (2.2, 2.6) if big_door else (1.0, 2.0)
    b.box("timber", (0, front, PLINTH + door_h / 2), (door_w, 0.08, door_h))
    b.box("timber", (0, front - 0.03, PLINTH + door_h + 0.1), (door_w + 0.4, 0.12, 0.2))  # lintel
    if big_door:
        return
    for sy in (-1, 1):
        y = sy * (d / 2 + 0.05)
        for x in (-w / 3, w / 3):
            z = PLINTH + h * 0.58
            b.box("dark", (x, y, z), (0.8, 0.06, 0.75))
            b.box("timber", (x, y + sy * 0.03, z + 0.42), (1.0, 0.1, 0.1))
            b.box("timber", (x, y + sy * 0.03, z - 0.42), (1.0, 0.1, 0.1))
            for s in (-1, 1):  # open shutters
                b.box("timber", (x + s * 0.72, y + sy * 0.06, z), (0.42, 0.05, 0.8))


def house(name, w, d, h, wall, frame, chimney, big_door):
    b = Builder()
    b.box("stone", (0, 0, PLINTH / 2), (w + 0.25, d + 0.25, PLINTH))
    b.box(wall, (0, 0, PLINTH + h / 2), (w, d, h))
    rise = d * 0.62  # steep thatch pitch (~51°)
    b.prism_x(wall, -w / 2, w / 2, PLINTH + h, d / 2, rise)
    if frame:
        timber_frame(b, w, d, h)
    roof(b, w, d, PLINTH + h, rise, chimney)
    openings(b, w, d, h, big_door)
    return b.to_object(name)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for name, spec in VARIANTS.items():
        obj = house(name, *spec)
        for o in bpy.context.scene.objects:
            o.select_set(o == obj)
        path = OUT / f"{name}.glb"
        bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True, export_yup=True)
        print(f"wrote {path} ({len(obj.data.polygons)} faces)")
        bpy.data.objects.remove(obj)


main()
