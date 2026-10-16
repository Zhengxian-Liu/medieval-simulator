"""Downloads the CC0 Poly Haven (and two ambientCG leaf atlases) textures/HDRI the village uses into godot/assets/ and writes assets/CREDITS.md.

Run: python3 godot/tools/fetch_polyhaven.py   (idempotent; skips files already present)
"""
import io
import json
import pathlib
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent / "assets"
API = "https://api.polyhaven.com"
TEXTURES = {  # material slot -> Poly Haven asset id
    "grass": "leafy_grass",
    "riverbed": "brown_mud_rocks_01",
    "stone": "stacked_stone_wall",
    "plaster": "grey_plaster_03",  # uneven lime/daub, mid value (white_rough_plaster read as modern render, near white)
    "timber": "weathered_planks",
    "thatch": "thatch_roof_angled",
    "rock": "rock_face",
}
MAPS = {"Diffuse": "albedo", "nor_gl": "normal", "Rough": "roughness"}  # Poly Haven key -> our suffix
HDRI = "kloofendal_48d_partly_cloudy_puresky"
MODELS = ["rock_moss_set_01", "rock_moss_set_02", "boulder_01", "rock_07", "rock_09", "shrub_02", "shrub_04", "pine_sapling_small"]  # 1K glTF; full-size Poly Haven trees are 100-950 MB, too heavy
AMBIENTCG = {"leaves_oak": "LeafSet016", "grass_cards": "LeafSet026"}  # leaf atlases with opacity (Poly Haven has none)
HEADERS = {"User-Agent": "MedievalSim-asset-fetch/1.0"}  # the API answers 403 to urllib's default agent


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as r:
        return json.load(r)


def fetch(url, dest):
    if dest.exists():
        return 0
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as r:
        data = r.read()
    dest.write_bytes(data)
    return len(data)


def main():
    total = 0
    credits = ["# Asset credits", "", "Downloaded assets are CC0 1.0 (public domain): [Poly Haven](https://polyhaven.com) unless marked ambientCG.", ""]
    for slot, asset in TEXTURES.items():
        files = get_json(f"{API}/files/{asset}")
        for key, suffix in MAPS.items():
            entry = files[key]["1k"]["jpg"]
            total += fetch(entry["url"], ROOT / "textures" / slot / f"{suffix}.jpg")
        credits.append(f"- `textures/{slot}/`: [{asset}](https://polyhaven.com/a/{asset}) (1K: albedo, normal GL, roughness)")
    for asset in MODELS:
        gltf = get_json(f"{API}/files/{asset}")["gltf"]["1k"]["gltf"]
        folder = ROOT / "models" / asset
        total += fetch(gltf["url"], folder / f"{asset}.gltf")
        for rel, entry in gltf.get("include", {}).items():
            total += fetch(entry["url"], folder / rel)
        credits.append(f"- `models/{asset}/`: [{asset}](https://polyhaven.com/a/{asset}) (1K glTF)")
    for slot, asset in AMBIENTCG.items():
        folder = ROOT / "textures" / slot
        if not folder.exists():
            url = f"https://ambientcg.com/get?file={asset}_1K-JPG.zip"
            with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS)) as r:
                data = r.read()
            total += len(data)
            folder.mkdir(parents=True)
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for name in z.namelist():  # images only; flatten names to <map>.jpg
                    if name.lower().endswith(".jpg"):
                        suffix = name.rsplit("_", 1)[-1].lower()  # e.g. Color.jpg, Opacity.jpg, NormalGL.jpg
                        (folder / suffix).write_bytes(z.read(name))
        credits.append(f"- `textures/{slot}/`: [{asset}](https://ambientcg.com/a/{asset}) (ambientCG, CC0 1.0, 1K)")
    hdr = get_json(f"{API}/files/{HDRI}")["hdri"]["2k"]["hdr"]
    total += fetch(hdr["url"], ROOT / "sky.hdr")
    credits.append(f"- `sky.hdr`: [{HDRI}](https://polyhaven.com/a/{HDRI}) (2K HDRI)")
    credits += ["", "Generated in this repo (no third-party source): `models/buildings/`, `models/props/`, `models/foliage/`, `models/trees/` by `tools/blender/build_asset.py`."]
    (ROOT / "CREDITS.md").write_text("\n".join(credits) + "\n")
    print(f"downloaded {total / 1e6:.1f} MB into {ROOT}")


if __name__ == "__main__":
    main()
