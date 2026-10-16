# Art check

Result: **FAIL** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| info | far masks | lit 55%, shadow 27%, key warmth 1.06 |  | yes | share of the frame classified lit / in shadow |
| value | far.lit_mean | 0.401 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | far.shadow_tint | 1.066 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | far.contact_floor | 0.082 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | far.whole_frame | 0.351 | 0.25-0.4 | yes | overall value structure |
| value | far.shadow_green | 0.987 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | far.roof_macro | 0.065 | 0.06-1.0 | yes | 16 px-blurred std dev on the barn roof >= 0.06 (review 5: lowering it to 0.05 was fitting the check; restored) |
| value | far.grass_speckle | 0.002 | 0.0-0.02 | yes | share of open-grass pixels brighter than median+0.15: tufts/debris must not read as sprinkles (region left of the stall) |
| value | far.saturation | 0.320 | 0.28-0.35 | yes | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | far.empty_share | 0.097 | 0.0-0.1 | yes | share of ~1.3 m blocks with almost no detail: The Guild never shows an empty patch (2026-10-16: 0.26) |
| value | far.key_fill | 4.479 | 3.0-5.0 | yes | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| info | mid masks | lit 35%, shadow 54%, key warmth 1.07 |  | yes | share of the frame classified lit / in shadow |
| value | mid.lit_mean | 0.447 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | mid.shadow_tint | 1.089 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | mid.contact_floor | 0.051 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | mid.whole_frame | 0.278 | 0.2-0.4 | yes | overall value structure |
| value | mid.shadow_green | 0.980 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | mid.saturation | 0.266 | 0.28-0.35 | **NO** | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | mid.key_fill | 9.449 | 3.0-5.0 | **NO** | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| value | night.mean | 0.112 | 0.1-0.2 | yes | night readable like Baldur's Gate 2 (~0.15), not black (user default) |
| value | night.torch_brightest | 2.366 | 1.8-10.0 | yes | mean of the torch's light pool / frame mean: the torch is clearly the brightest area at night |
| value | dusk.lit_mean | 0.231 | 0.22-0.5 | yes | whole frame at golden hour stays a daylight frame (review 5). 0.30 -> 0.22, user decision 2026-10-16: a 16-17 deg sun lights flat ground at ~0.29 of noon, so golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.key_warmth | 1.566 | 1.5-2.5 | yes | red/blue of the light on sunlit surfaces (lighting-only pass): noon sun [1,0.96,0.9] gives ~1.1; golden hour clearly warm (>= 1.5) but not sunset orange (<= 2.5). Set before measuring. user decision 2026-10-16 (“改看暖色”): golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.key_fill | 4.108 | 3.0-6.0 | yes | a low sun still models form: key:fill at least the daytime 3, up to 6 (long, deeper shadows). Set before measuring. user decision 2026-10-16 (“改看暖色”): golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.shadow_tint | 1.163 | 0.87-1.3 | yes | blue/red of the light in shadow (lighting-only pass): at golden hour only the key is warm, the shade stays neutral or cool (Fable round 6: R/B <= 1.15) |
| value | dusk.saturation | 0.387 | 0.2-0.4 | yes | whole-frame HSL saturation: golden hour, not an orange filter (round 6 measured 0.48 vs 0.33 at noon) |
| value | morning.key_warmth | 1.098 | 0.95-1.15 | yes | red/blue of the key light on lit pixels: noon measures 1.05; a visibly orange morning would read > 1.15 |
| mipmaps | 61 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 44598 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 75086 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 89858 | <= 100000 | yes | triangles |
