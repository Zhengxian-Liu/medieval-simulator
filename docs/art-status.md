# Art loop status (paused 2026-10-16 at the user's request)

Resume from here. Loop and rules: `docs/art-pipeline.md`; check conflicts and decisions: `docs/art-check-conflicts.md`.

## Last measured (`docs/art-check.md`, before the uncommitted-then-committed changes below)
FAIL: far.roof_macro 0.056 (>= 0.06), far.empty_share 0.278 (<= 0.10), far.key_fill 2.12 (3-5), mid.contact_floor 0.039
(>= 0.05), mid saturation 0.248 (0.28-0.35), dusk.lit_mean 0.238 (target now >= 0.22, should pass). Last Fable score: round 5.

## Changed but NOT yet captured or measured (commit "wip: ...")
- `sun.angular_distance` 0.5 -> 1.5 (soft penumbra, coordinator default).
- Wall plaster texture `white_rough_plaster` -> `grey_plaster_03` (uneven lime/daub, mid value; `fetch_polyhaven.py`).
  Godot reimports it on the next run; `materials.weathered.plaster.tint` was tuned for the old near-white scan, re-check.
- `checks.dusk.lit_mean` min 0.30 -> 0.22 (user decision).

## Next steps, in order
1. Wire the dusk checks the user approved: capture.sh renders the fill/light passes for `godot_far_dusk` too
   (`fill=1`); `art_check.py` dusk loop uses `masked()`; add `checks.dusk.warmth` (key-light R/B on the lighting-only
   pass, lit pixels; noon ~1.1, proposed 1.5-2.5) and `checks.dusk.key_fill` (proposed 3-6). Pick final targets from a
   measured noon baseline and the references; record reasons in look.json.
2. Run `python3 godot/tools/art_check.py`; re-tune plaster tint if mid saturation / contact move.
3. Far key:fill: measure on shadowed open ground only (hypothesis: roof slopes see the whole sky), then decide.
4. Contact floor: lower stacked darkening at footings (vertex AO strength, SSAO light affect), not global fill.
5. Thatch: subtle horizontal courses with per-course variation in `weathered.gdshader` (vertex colour B = ridge->eave,
   course period ~0.28 m, matching `roofs.py`), parameters in `materials.weathered.thatch`.
6. Density for empty_share: more houses / clutter in `village.json`.
7. Disco Elysium items as look.json toggles with A/B shots (off by default), Fable judges.
8. Fable round 6.
