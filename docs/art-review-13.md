# Art review, round 13 (Fable, 2026-10-17) - vegetation + ground round

Overall **6.9** (r12 6.9). Lighting 7, time of day 7, materials 7, thatch 6.5 (unchanged by decision), ground 6.5,
foliage 6.8, density 6.5, Guild feel 6.5. Neither vegetation nor ground at 7 yet.

Ground: lawn colour right (olive, green ratio 1.31-1.32 inside the Guild range). The mottling is invisible (a hue shift
with the max channel unchanged, one noise scale, a dead band): lit-grass 16 px sd 0.041 far / 0.054 mid. Next: patches
that change value as well as hue (drier ~0.85-0.9x, lighter worn), two noise scales (~0.45/m, ~0.15/m), no dead band,
amplitude set against a clean-rect Guild lit-lawn sd. Dirt: uniform gravel speckle; needs a scan with 0.5-2 m structure.
Vegetation: hard-key atlas works (dark undersides, self-shadow; leaf p50 0.37 -> 0.40). No crown-level cap: crown p10/p90
0.11/0.53 vs the Guild's one deciduous crown (guild3_3 250x150+1000+0) 0.10/0.68; sky-facing and side cards get the same
light. Next: crown-space normals or a top-down light gradient; split bushes in the foliage checks (bush p90 0.53 vs shrub
0.73); sink the straight dark twig cards inside the leaf volume.
Regression to check: mid lit-dirt fine detail 0.106 -> 0.070 (cause not identified).
