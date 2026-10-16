# Art loop status (paused 2026-10-17 at the user's request)

Loop and rules: `docs/art-pipeline.md`. Decisions, conflicts and rejected A/Bs: `docs/art-check-conflicts.md`.
User decisions in force: far.whole_frame cap 0.46; one class per round until each scores >= 7 - vegetation and ground
first, then thatch (asset rework approved: straw texture with bundles + hanging eave fringe, CC0 or self-baked).

## Where it stands
- Last Fable score: round 13, overall 6.9 (`docs/art-review-13.md`): lighting 7, time of day 7, materials 7, thatch 6.5,
  ground 6.5, foliage 6.8, density 6.5, Guild feel 6.5.
- `python3 godot/tools/art_check.py`: all pass except far.thatch_bundles 0.050 (>= 0.07, thatch round),
  mid.key_fill 9.9 (wall-side sky occlusion, documented), dusk.saturation 0.414 (<= 0.40; greener grass - proposed to
  measure relative to noon, needs a decision). `simcore/scripts/test.sh`: all pass.

## Next steps, in order
1. Vegetation (round 14): crown-level sunlit cap (crown-space normals or a top-down light gradient so sky-facing cards
   take the key; target the Guild deciduous crown p90 ~0.62-0.68 measured with the same method); sink the dark twig
   cards inside the leaves; add a separate bush check (bushes p90 0.53 vs the Guild shrub 0.73) and brighten bushes.
2. Ground (round 14): grass mottling that changes value as well as hue, two noise scales, no dead band, amplitude from a
   clean Guild lawn rect; dirt with 0.5-2 m structure (a better CC0 scan, or rut/pebble geometry - painted stones were
   rejected twice); check the mid lit-dirt fine-detail drop (0.106 -> 0.070).
3. dusk.saturation: decide whether to measure it relative to the noon frame.
4. Thatch round: reed_roof_04 texture (A/B ab/thatch_reed2) or a Blender-baked straw tile, plus hanging eave fringe
   cards; target far.thatch_bundles >= 0.07.
