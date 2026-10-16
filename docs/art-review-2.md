# Art review — Godot frames vs pre-rendered target

All values are sRGB region means on the colour PNGs: `magick F.png -crop WxH+X+Y +repage -colorspace gray -format "%[fx:mean]" info:`.

## 1. Scores

- Lighting/value **4**: godot_far has no sun shadows at all (pole, capsule, bushes, houses; the dusk frame proves the omni shadows). godot_mid lit:shadowed ground = 0.44:0.06 (≈30:1 linear, not the intended 4:1); shadowed wall 0.05. The reference panels, blurred, never go below ≈0.10 anywhere.
- Grounding **4**: nothing in far is pinned down; in mid, shadowed objects vanish.
- Materials **5**: cottage build is good, but barn planks read 0.09 unshadowed (far, right third), thatch is periodic stripes, the square is uniform pebble speckle, water is a flat two-tone with a ruler-straight bank.
- Foliage/grass **4**: tufts are countable dark starbursts (far, left third); bushes are lime at 2× ground brightness (far top, river); firs are untextured diamond cards (river, right).
- Silhouettes/density **4**: the square is an empty 8 m disc; the reference never shows ~2 m without a prop, shadow or ground change.
- Pre-rendered feel **4**.
- **Overall 4.5/10.** A 9: every surface in sun and shade keeps mid-value detail, every object sits in a soft-edged sun shadow, the ground has large tonal shapes (ruts, damp, worn fringes) instead of speckle, and the yards are cluttered enough that the eye never lands on a flat patch.

## 2. Changes, by payoff per effort

**A. Far view has no directional shadows (bug).** Far pose: distance = 16/(2·tan6°·16/9) ≈ 42.8 m, camera height ≈ 33.7 m, so visible ground lies 40–47 m away; main.gd:116 `directional_shadow_max_distance = 40.0` ends before it. The band is narrow, so use `SHADOW_ORTHOGONAL` (one cascade, no split seams) and derive in `_apply_camera`: `max_distance = height / sin(pitch − fov/2) + 3` (≈50 m far, ≈19 m mid); also raise `camera.near` at far (~25 m) so the fit starts at the scene, checking a tall tree at the bottom edge is not clipped. Verify: `tools/capture.sh`; far pole and barn cast shadows.

**B. Shadow floor and black timber.** One knob at a time, re-measure after each: main.gd:95 `adjustment_contrast = 1.1` → 1.0; then `ambient_light_energy` 0.7 → ~1.0 with ambient colour 0.6 grey; then weathered.gdshader splash 0.75 → 0.4, grime 0.35 → 0.15, timber `tint` ×1.4. Target: lit ground 0.45–0.50, shadowed ground 0.20–0.24 (≈4:1 linear), unshadowed planks ≥ 0.18.

**C. Ground macro-shapes.** Riverbed pebbles at 0.4 tiles/m everywhere; grass and square both measure 0.42. ground.gd `_line`: paint two ruts (×0.75, 0.4 m apart) per path and a worn fringe (mask 0.3–0.5 → yellowed grass, no tufts); ground.gdshader: 0.05-frequency noise multiply 0.72–1.0 (damp patches), dirt_tiles 0.18. Verify: far square shows ≥3 distinct tonal shapes; square and grass means differ by ≥ 0.06.

**D. Tufts and foliage colour.** `_tuft_mesh`: height 0.05–0.12, blades fan from a 0.15 m base, tip = ground tint ×1.15, keep threshold 0.15 → 0.45. leaves.gdshader `tint` → (0.33,0.42,0.26), variation 0.35. Firs: oaks only inside 20 m, or texture make_trees.py cards with the existing pine_sapling_small set. Verify: far left third, no tuft countable; bush mean ≤ 1.2× grass; river frame has no bare triangle edges.

**E. Thatch stripes.** `stagger_courses` scales v per course → corduroy. Per-course u offset only, ±4 % brightness via vertex colour, 0.5 m bundles with own offset, eave verts displaced 0–8 cm. Verify: far right third, no banding at 25 % zoom.

**F. Yard density.** `_dress_yard`: log pile, 2–3 barrels, nettles (bushes at 0.3–0.5 against walls), puddle decal; square gets a well and stall. Verify: far frame, no empty patch > 3 m².

## 3. Done bar (overall ≥ 8, no area < 7)

- Lighting ≥7: all hour-11 frames show sun shadows from every object; shadowed ground 0.20–0.24 with lit 0.45–0.50; no blurred region < 0.10 outside true crevices.
- Grounding ≥7: every prop/bush/house has contact and cast shadow; nothing reads pasted on.
- Materials ≥7: unshadowed planks ≥ 0.18; no thatch banding; water has a wet-bank transition and ripple normals.
- Foliage/grass ≥7: tufts not countable at far; bush ≤ 1.2× grass; no untextured cards.
- Density ≥7: no empty patch > 3 m² in far.
- Pre-rendered feel ≥8: beside the reference at 50 % zoom, no real-time tell (hard shadow edge, black shadow, flat water, starburst tufts, polygonal trees).
- Rerun: `tools/capture.sh` plus the `magick` means above.
