"""Single entry point of the Blender asset pipeline (see docs/art-pipeline.md).

  Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- <kind> [name ...]
      kind: building | prop | foliage | tree | all          (no names = every asset of that kind)
  Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- atlas   (leaf-cluster atlas, before foliage)
  Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- preview <building> <out_dir> [samples]

Every asset: fresh scene, deterministic RNG seeded by name, build from its spec, then
  buildings/props: join -> Cycles-baked AO + convexity into COLOR_0 -> GLB without images (export.bake_and_export)
  foliage/trees:   their own vertex colours (canopy depth / needle shade) -> GLB without images
into godot/assets/models/<kind dir>/<name>.glb, and prints triangles against art/look.json checks.budgets.
"""
import json
import zlib
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import bpy  # noqa: E402

import buildings  # noqa: E402
import export  # noqa: E402
import foliage  # noqa: E402
import kit  # noqa: E402
import props  # noqa: E402
import trees  # noqa: E402

GODOT = pathlib.Path(__file__).resolve().parents[2]
MODELS = GODOT / "assets" / "models"
BUDGETS = json.loads((GODOT / "art" / "look.json").read_text())["checks"]["budgets"]["v"]


def fresh(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    kit._materials.clear()
    kit.RNG.seed(zlib.crc32(name.encode()))  # stable across runs (str hash() is randomised per process)


def report(kind, name, tris):
    budget = BUDGETS["building" if kind == "building" else "foliage" if kind in ("foliage", "tree") else "prop"]
    flag = "" if tris <= budget else f"  OVER BUDGET ({budget})"
    print(f"build_asset: {kind} {name}: {tris} triangles{flag}")
    return tris <= budget


def build_building(name):
    fresh(name)
    buildings.build(name)
    return report("building", name, export.bake_and_export(name, MODELS / "buildings" / f"{name}.glb"))


def build_prop(name):
    fresh(name)
    props.PROPS[name]()
    return report("prop", name, export.bake_and_export(name, MODELS / "props" / f"{name}.glb"))


def build_foliage(name):
    fresh(name)
    obj = foliage.build(name, *foliage.VARIANTS[name])
    export.export_glb(obj, MODELS / "foliage" / f"{name}.glb")
    return report("foliage", name, export.triangles(obj))


def build_tree(name):
    fresh(name)
    obj = trees.fir(int(name.split("_")[1]))
    export.export_glb(obj, MODELS / "trees" / f"{name}.glb")
    return report("tree", name, export.triangles(obj))


KINDS = {
    "building": (build_building, lambda: list(buildings.VARIANTS)),
    "prop": (build_prop, lambda: list(props.PROPS)),
    "foliage": (build_foliage, lambda: list(foliage.VARIANTS)),
    "tree": (build_tree, lambda: [f"fir_{i}" for i in range(trees.VARIANTS)]),
}


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    if args[0] == "atlas":  # leaf-cluster atlas for the foliage cards (leaf_atlas.py); run before `foliage`
        import leaf_atlas
        bpy.ops.wm.read_factory_settings(use_empty=True)
        leaf_atlas.build()
        return
    if args[0] == "preview":
        import preview
        fresh(args[1])
        buildings.build(args[1])
        preview.stage()
        preview.render(args[2], int(args[3]) if len(args) > 3 else 64)
        return
    kinds = list(KINDS) if args[0] == "all" else [args[0]]
    ok = True
    for kind in kinds:
        build, everything = KINDS[kind]
        for name in (args[1:] if args[0] != "all" and len(args) > 1 else everything()):
            ok = build(name) and ok
    if not ok:
        sys.exit(1)


main()
