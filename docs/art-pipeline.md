# Art pipeline (Godot village)

Target: environment and building quality of a classic pre-rendered isometric game (Commandos 2, Baldur's Gate,
Diablo II) and of Path of Exile / Diablo IV — realistic materials, daylight, neutral grade, no dark mood.
The quality bar is judged by Fable (art director) against `docs/reference/` (the pre-rendered indication and The Guild
screenshots in `docs/reference/the-guild/`, breakdown in `docs/visual-target.md`) and by `tools/art_check.py` numbers.
Done = Fable overall ≥ 8 with no area < 7, and `art_check.py` passes.

Everything below is a rule; every number lives in **`godot/art/look.json`** (each value with its reason). Change a
number there, never in scene code or shaders. Update this document in the same commit as any rule change.

## 1. The loop

1. Change a value in `look.json`, or a builder in `godot/tools/blender/`.
2. Rebuild assets if geometry changed: `Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- all`
   (or `-- prop barrel`, `-- building cottage` ...).
3. `python3 godot/tools/art_check.py` → captures far/mid/dusk/river shots, writes greyscale copies, measures, writes
   `docs/art-check.md`, exits 1 on failure.
4. Ask Fable to score the shots + `docs/art-check.md`; turn its findings into new `checks` entries where measurable.
5. Commit with the shots.

## 2. Parameters (`godot/art/look.json`)

| group | what it controls |
|---|---|
| `camera` | far/near FOV, framed width, pitch, yaw (read by `scripts/camera_zoom.gd`) |
| `environment` | AgX tonemap, contrast/saturation (neutral), ambient fill (neutral grey share, energy), SDFGI, SSAO, SSIL |
| `sun` | energy, angular size, path (peak elevation, yaw), colours, shadow bias, shadow range margin |
| `materials` | triplanar tiling/tint, per-slot weathered params (`weathered.<slot>`), shared weathering, leaves, water, ground, flat placeholder colours |
| `placement` | skirt decal, grass tufts, debris density, tree clearance, props kept off paths |
| `checks` | measured regions + targets per shot, triangle budgets |

Read in code with `scripts/look.gd`: `Look.v("sun.energy_day")`, `Look.col(...)`, `Look.group(...)`,
`Look.apply(shader_material, params)`.

## 3. Assets (Blender, `godot/tools/blender/`)

One entry: `build_asset.py -- <building|prop|foliage|tree|all> [names]`; `-- preview <building> <out_dir>` renders a
Cycles preview. Every asset: fresh scene, RNG seeded with CRC32 of its name (deterministic), build, then:

- **buildings / props** → `export.bake_and_export`: join parts, bake **vertex colour R = Cycles AO** (with a ground
  plane, so contact darkens) and **G = convexity** (edge wear), export GLB **without images**.
- **foliage / trees** → their own vertex colours (canopy depth / needle shade), GLB without images.

Shared modules:

| module | provides |
|---|---|
| `kit.py` | `new_object` (box UVs with V along the grain, named material slot), `cube_bm` (bevel/subdiv/jitter), `beam` (bevelled, slightly bowed timber), `stone` (irregular), `lathe`, `log`, Cycles preview materials |
| `bake.py` | `bake_ao`, `bake_edges` |
| `export.py` | `join_all`, `bake_and_export`, `export_glb`, `triangles` |
| `roofs.py` | `thatch_roof` (layered courses, ragged eave, sag between gables, wavy ridge, lumpy ridge cap) |
| `buildings.py` | `VARIANTS` spec per building + footing, walls, timber frame, openings, chimney |
| `props.py` | one function per prop + `PROPS` table |
| `foliage.py` / `trees.py` | oaks + bushes (leaf cards) / background firs |

**Material slots** are the contract with Godot: `stone, plaster, timber, thatch, cloth, iron, dark` (+ `bark`,
`leaves` for foliage). Each slot's look is `materials.weathered.<slot>` in `look.json` (tint, dirt height, moss,
detile, texture override) on top of `materials.weathered_common`.

### Add a building
1. Add a line to `VARIANTS` in `buildings.py` (footprint, wall height, infill slot, chimney, door, windows).
2. `build_asset.py -- building <name>`; check triangles against `checks.budgets.building`.
3. Place it in `scripts/village.gd` (`houses` list); add its footprint to `FOOTPRINT` for yard dressing.
4. `art_check.py`.

### Add a prop
1. Write a function in `props.py` using the kit (every part through `new_object` with a slot and grain axis).
2. Add it to `PROPS`; `build_asset.py -- prop <name>`.
3. Place with `_prop(root, "<name>", pos, yaw)` — skirt decal, weathered materials and path rules apply automatically.

### Add a texture set
Add it to `godot/tools/fetch_polyhaven.py` (Poly Haven / ambientCG, CC0 only, 1K), run it; credits are written to
`godot/assets/CREDITS.md`. Add a `materials.weathered.<slot>` entry if it is a new slot.

## 4. Placement rules (applied automatically in `scripts/village.gd` / `ground.gd`)

- Every placed building/prop/rock gets a **dirt skirt decal** sized from its bounds (`placement.skirt`).
- Every scripted asset gets the **weathered shader** per slot (baked AO → ambient occlusion + crevice grime, convexity
  → edge wear, height → splash dirt, normal → moss, noise → roughness variation).
- Props in `placement.keep_off_paths` are dropped if placed on trampled ground.
- Trees respect `placement.tree_clearance` (river, houses, paths); broadleaf oaks within 25 m, firs only beyond.
- Ground: grass/dirt blend by the path mask, worn fringe, large damp patches; grass tufts clustered by noise with a
  dead zone on paths; debris (pebbles, twigs, chips, leaves) densest on path fringes and around houses.
- Godot import defaults (`project.godot [importer_defaults]`): VRAM-compressed textures **with mipmaps**; 16x
  anisotropic filtering, TAA + MSAA, 8K soft shadows.

## 5. Checks (`tools/art_check.py`, targets in `look.json checks`)

- Lit / shadow classification: each far/mid shot is rendered twice, sun on and sun off; a pixel is lit if the sun adds
  > 5x, in shadow if < 3x (bounce only). Means of those sets are checked, so checks follow the shadows.
- Values: lit 0.40–0.50; shadow 0.20–0.26 far (0.16 mid, dark timber wall in frame); whole frame 0.25–0.40 far.
- Contact: 1st percentile of the 8 px-blurred frame ≥ 0.05 (dark contacts, never black halos).
- Neutral shadow light (lighting-only pass): blue/red 0.85–1.2, green/avg(red,blue) 0.95–1.05.
- Saturation (HSL mean, whole frame) 0.28–0.35: colour from materials, neutral grade.
- Grass speckle ≤ 2% of open-grass pixels brighter than median + 0.15.
- Roof macro structure: 16 px-blurred std dev on the barn roof ≥ 0.06.
- Every texture mipmapped; every GLB within its triangle budget.
- Screenshots: `godot/tools/capture.sh` renders each shot in its own Godot process at 3840×2160 → 1920×1080.

## 6. Lessons learned (each cost a round; do not repeat)

| symptom | cause | rule now |
|---|---|---|
| shimmering, "20-year-old 3D game" | textures imported without mipmaps | importer defaults generate mipmaps; `art_check` audits it |
| far view had no sun shadows | camera ~43 m from the ground, shadow range fixed at 40 m | one orthogonal cascade fitted to the visible depth every zoom (`main.gd _apply_camera`) |
| black triangle on gables | vertex AO on a 3-vertex face: all corners under the eave | subdivide large flat faces before baking |
| diagonal stripes on thatch | one texture period across the whole roof; per-course UV offsets/scales then made corduroy seams | no per-course UV tricks: shader detile sample (0.6) + patch noise |
| 1.8 MB barrels | glTF exporter embedded every texture | export with `export_image_format="NONE"`; Godot textures by slot |
| assets changed every build | Python `hash(str)` is randomised per process | seed with `zlib.crc32(name)` |
| washed-out / black grass and trees | vertex colours authored as sRGB read as linear | `vertex_color_is_srgb = true` on those materials |
| blue-grey shadows | fill came entirely from the blue HDRI sky | part of the fill is neutral grey (`environment.ambient_*`) |
| crowns over the bridge/river | tree clearance smaller than crown radius | `placement.tree_clearance.river` ≥ crown radius |
| captures hang after the first shot | macOS pauses a covered window | `capture.sh`: one process per shot, always-on-top, caffeinate, retry |
| loud colours, hard edges | ACES + contrast boost | AgX, neutral contrast (user preferred the Cycles look) |
| changing ambient energy did nothing | with SDFGI on, Godot takes sky/bounce light from SDFGI and ignores ambient energy/colour | the fill knob is `environment.sdfgi_energy` |
| blue shadows came back after every fill change | under SDFGI the HDRI sky is the fill light | sky drawn by `shaders/sky.gdshader`, desaturated (`environment.sky_saturation`) with its own energy (`sky_energy`, the shadow-floor knob) |
| shadow colour checks confused material colour with light colour | warm soil / green grass skew the ratios | capture renders a lighting-only pass (`_light.png`, white albedo); tint checks read the light there |
| grass tufts black / dark starbursts | culling off flips the normal on back faces, pointing them down | emit both windings with an up normal, cull back faces |
| frame grey (saturation 0.17 vs 0.30-0.40 in references) | colour came only from desaturated scans + neutral grade | chroma lives in material tints (grass, soil, thatch, daub, timber, leaves); `checks.*.saturation` 0.28-0.35 |
| fixed check rectangles broke when the sun moved | shadows move with the sun | `capture.sh` also renders far/mid with the sun off; `art_check.py` classifies lit/shadow pixels from the pair |
