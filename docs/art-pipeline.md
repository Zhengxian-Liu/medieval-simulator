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
   (or `-- prop barrel`, `-- building cottage` ...). Leaf-cluster atlas first when foliage or leaves change: `-- atlas`.
3. `python3 godot/tools/art_check.py` → captures far/mid/dusk/river shots, writes greyscale copies, measures, writes
   `docs/art-check.md`, exits 1 on failure.
4. A/B a candidate change without committing it: `python3 godot/tools/ab.py <label> <path=value> ...` →
   `docs/screenshots/ab/<label>/<shot>_ab.jpg` (current left, candidate right).
5. Ask Fable to score the shots + `docs/art-check.md`; turn its findings into new `checks` entries where measurable.
6. Commit with the shots.

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
  plane, so contact darkens), **G = convexity** (edge wear) and **B = thatch ridge→eave position**, export GLB **without images**.
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
| `leaf_atlas.py` | leaf-cluster atlas: 6 clumps of 30 CC0 oak leaves rendered in Cycles under a 45 deg key + dim sky + dark ground (lit tops, self-shadow, dark undersides) -> `assets/textures/leaf_clusters` (1K) + `atlas.json` (mean opacity, read by foliage.py for the card count) |
| `foliage.py` / `trees.py` | oaks + bushes (leaf-cluster cards, count from lobe surface x cover / card coverage) / background firs |

**Material slots** are the contract with Godot: `stone, plaster, timber, thatch, cloth, iron, dark` (+ `bark`,
`leaves` for foliage). Each slot's look is `materials.weathered.<slot>` in `look.json` (tint, dirt height, moss,
detile, texture override) on top of `materials.weathered_common`.

### Village layout
`godot/art/village.json`: square position, houses (x, z, yaw, type) and footprints. Adding a house is one line; the
placement rules dress it automatically (paths to the door, yard props, woodpile under the lean-to, kitchen garden
behind, a yard tree, bushes at the corners).

### Add hand-placed content
`village.json` `props` ([name, x, z, yaw]), `trees` ([x, z, scale]), `bushes` ([x, z]). To find where the frame is empty,
the `far.empty_share` blocks are 160 px squares of the 1920 px shot; fill the blocks with sd < 0.04.

### Add a building
1. Add a line to `VARIANTS` in `buildings.py` (footprint, wall height, infill slot, chimney, door, windows, lean_to).
2. `build_asset.py -- building <name>`; check triangles against `checks.budgets.building`.
3. Place it in `godot/art/village.json` (`houses`), with its footprint under `footprints`.
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
- Values: lit 0.40–0.50; whole frame 0.25–0.40 far. Shadow depth is checked as **key:fill 3–5** on the lighting-only pass
  (lit light / shadow light), not as a displayed shadow value: that mixed material albedo into a lighting rule
  (evidence: `docs/art-check-conflicts.md`).
- Per material (material-ID pass `_id.png`, classes in `checks.id_colors`): `class_luma`, `class_saturation`,
  `class_luma_ratio` on lit pixels of one material (e.g. thatch vs grass, foliage value).
- Contact: 1st percentile of the 8 px-blurred frame ≥ 0.05 (dark contacts, never black halos).
- Neutral shadow light (lighting-only pass): blue/red 0.85–1.2, green/avg(red,blue) 0.95–1.05.
- Saturation (HSL mean, lit pixels) 0.28–0.35: colour from materials, neutral grade.
- Grass speckle ≤ 2% of open-grass pixels brighter than median + 0.15.
- Roof macro structure: 16 px-blurred std dev on the barn roof ≥ 0.05.
- Dusk (`godot_far_dusk`, 16:48, sun ~17 deg; user decision 2026-10-16): frame >= 0.22, key warmth (R/B of the key light
  on lit pixels, lighting-only pass) 1.5–2.5, key:fill 3–6; shade stays neutral (B/R >= 0.87) and the frame is not an
  orange filter (saturation <= 0.40).
- Morning (`godot_far_morning`, 9:00): key warmth 0.95–1.15, daylight stays neutral under the golden-hour ramp.
- Quick A/B: `python3 godot/tools/ab.py <label> <path=value> ...` renders, writes side-by-sides and prints B's numbers
  (`art_check.py --dir docs/screenshots/ab/<label>`).
- Night (`godot_far_night`, hour 22): frame mean 0.10–0.20 (moonlit, readable), torch pool ≥ 1.8x the frame mean.
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
| albedo changes barely showed on the thatch | glossy scanned roughness + specular 0.5: the roof's brightness was mostly sky reflection | per-slot `roughness_min` / `specular` in `materials.weathered` (straw 0.9 / 0.15) |
| roof one flat plane | open-sky AO is uniform; noise patches cluster near 0.5 | bake ridge→eave position into vertex colour B (`roofs.py` "slope" → `bake.py`), shader `eave_age` + stretched bundle patches |
| a check passed only because of an accident | the square's torch was lit all day and brightened the mid-view shadows | torches are lit only below `placement.torch.lit_below_day`; re-verify checks after any light change |
| checks fitted to the frame | lowering a threshold until it passes (roof_macro 0.06 -> 0.05, mid shadow 0.20 -> 0.16) | never relax a target to pass; change the frame, or fix the measure if it measures the wrong thing (e.g. saturation of lit pixels only) |
| full-size yard trees hid the village | at 52° down a crown covers a quarter of the frame | `placement.yard_tree`: smaller oaks, behind houses, ≥ 10 m from the square |
| frame grey (saturation 0.17 vs 0.30-0.40 in references) | colour came only from desaturated scans + neutral grade | chroma lives in material tints (grass, soil, thatch, daub, timber, leaves); `checks.*.saturation` 0.28-0.35 |
| fixed check rectangles broke when the sun moved | shadows move with the sun | `capture.sh` also renders far/mid with the sun off; `art_check.py` classifies lit/shadow pixels from the pair |
| far and mid shadow targets could not both pass with one fill knob | displayed shadow value = light × albedo; the shadowed materials differ per shot (0.51× vs 1.88× the lit albedo) | lighting rules are measured on the lighting-only pass (`key_fill`); write down conflicting checks with evidence before tuning |
| key:fill and light warmth looked flat; far key:fill 2.1 vs mid 4.0 "under the same light" | the lighting-only pass went through AgX: lit light capped at ~0.89 and bleached | the lighting-only pass renders with the linear tonemapper at `checks.light_pass_exposure` (no clipping) |
| a swapped texture never showed up in the shots | running the game uses cached imports | `capture.sh` runs `godot --headless --import` first |
| golden hour looked like overcast dusk (sun on ~= sun off) | sun colour warmed only below 20 deg; sky fill stayed at full noon strength | `sun.warm_below_elevation`, `environment.sky_energy_low` (sky dims with the sun), `environment.exposure_low` (camera exposes up); all gated to daylight |
| plaster swap made walls a tan slab, half-timbering lost (round 6) | mid-value tan scan x warm tint: frame and infill same value | daub stays pale lime on the white flaking scan; the timber frame must read darker |
| golden hour read as an orange filter (round 6) | shade lit by orange bounce from the ground | sky saturation rises with a low sun (`environment.sky_saturation_low`): blue sky keeps shade neutral; `checks.dusk.shadow_tint`, `.saturation` |
| painterly toggles (Disco Elysium study) | world brush strokes at 0.12 were invisible (0.75/255); edge blur + desaturate read as a lens defect | rejected by Fable round 6 and removed; adopt only what survives an A/B (`tools/ab.py`) |
| bushes read as confetti at the mid view (round 6) | 17 cm leaf cards | bushes use real leaf size (~10 cm) and 3-4x the cards (`foliage.py` VARIANTS) |
| per-material targets fitted to Fable's eyeballed numbers | a reviewer's estimate (thatch 2.7x grass) was off by 2x vs the reference | measure the reference with the same method before setting a target |
| gable walls read as one 6 m daub slab (round 7) | frame() framed only the long walls | gable walls get ~1.2 m stud bays, a mid-rail and braces (`buildings.py frame`) |
| dark hexagon at the torch foot at night | the 6-sided post shadowed the ground under its own omni light | thin props under their own light don't cast shadows |
| key:fill differed by view for the same ground (far 2.4-3.6, mid ~6) | not SSAO, SSIL or SDFGI resolution (each A/B'd); partly wall-side geometry | measure per material/region with the ID pass; unresolved remainder recorded in art-check-conflicts.md |
| bushes still grew through props with a 1.1 m clearance (round 8) | bush crowns vary 0.9-1.7 m with scale | clearance from mesh bounds per instance (`_radius`, `placement.bush_crown_overlap`) |
| crown hull read as a smooth shaded sphere (round 8) | one radius, flat shade | lumpy hull per lobe, lit top / dark underside (`foliage.py inner`) |
| fixed check rectangles went stale when content moved (roof rect under the oak's shadow) | rects assume the frame | per-material checks on the ID pass (`class_*` measures) instead of rects |
| thatch read as shingles (round 9) | hard per-bundle cells (vertical seams + hard butt ends) lit flank by flank like tiles | bundle variation is smooth along the course (feathered stripes); only the courses step || oak crowns read as uniform confetti (rounds 7-9) | leaf lobes overlapped around the centre; moving them apart exposed bare twigs | each main branch carries its own leaf clusters with the twigs inside them (`foliage.py build`) |
| river read as milky water with white scratches (r5-r10) | full-strength value-noise ripple normals caught the sky in stretched cells | `materials.water.ripple` 0.35, darker shallows |
| grass and the mid frame stayed grey whatever the tint (rounds 5-11) | the ground used Godot's default specular 0.5: grazing sky reflection greyed it | ground `specular` 0.3 like the other matte slots; measure saturation per material (ID pass) |
| captures sometimes missed a pass (GPU fence timeout) | retry looked only at the main frame | `capture.sh` requires every pass and retries up to 3 times |
| crowns read as confetti or topiary (rounds 7-11) | one flat unshaded leaf per card | baked leaf-cluster cards (`leaf_atlas.py`); card count from crown surface - 60-120 per tree left it see-through |
