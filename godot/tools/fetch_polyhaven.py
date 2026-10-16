"""Downloads the CC0 Poly Haven textures/HDRI the village uses into godot/assets/ and writes assets/CREDITS.md.

Run: python3 godot/tools/fetch_polyhaven.py   (idempotent; skips files already present)
"""
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent / "assets"
API = "https://api.polyhaven.com"
TEXTURES = {  # material slot -> Poly Haven asset id
    "grass": "leafy_grass",
    "riverbed": "brown_mud_rocks_01",
    "stone": "stacked_stone_wall",
    "plaster": "white_rough_plaster",
    "timber": "weathered_planks",
    "thatch": "thatch_roof_angled",
    "rock": "rock_face",
}
MAPS = {"Diffuse": "albedo", "nor_gl": "normal", "Rough": "roughness"}  # Poly Haven key -> our suffix
HDRI = "kloofendal_48d_partly_cloudy_puresky"
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
    credits = ["# Asset credits", "", "All assets from [Poly Haven](https://polyhaven.com), licence CC0 1.0 (public domain).", ""]
    for slot, asset in TEXTURES.items():
        files = get_json(f"{API}/files/{asset}")
        for key, suffix in MAPS.items():
            entry = files[key]["1k"]["jpg"]
            total += fetch(entry["url"], ROOT / "textures" / slot / f"{suffix}.jpg")
        credits.append(f"- `textures/{slot}/`: [{asset}](https://polyhaven.com/a/{asset}) (1K: albedo, normal GL, roughness)")
    hdr = get_json(f"{API}/files/{HDRI}")["hdri"]["2k"]["hdr"]
    total += fetch(hdr["url"], ROOT / "sky.hdr")
    credits.append(f"- `sky.hdr`: [{HDRI}](https://polyhaven.com/a/{HDRI}) (2K HDRI)")
    (ROOT / "CREDITS.md").write_text("\n".join(credits) + "\n")
    print(f"downloaded {total / 1e6:.1f} MB into {ROOT}")


if __name__ == "__main__":
    main()
