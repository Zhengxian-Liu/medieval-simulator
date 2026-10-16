# Art check

Result: **FAIL** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| info | far masks | lit 58%, shadow 26% |  | yes | share of the frame classified lit / in shadow |
| value | far.lit_mean | 0.415 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | far.shadow_tint | 1.044 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | far.contact_floor | 0.071 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | far.whole_frame | 0.361 | 0.25-0.4 | yes | overall value structure |
| value | far.shadow_green | 0.992 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | far.roof_macro | 0.056 | 0.06-1.0 | **NO** | 16 px-blurred std dev on the barn roof >= 0.06 (review 5: lowering it to 0.05 was fitting the check; restored) |
| value | far.grass_speckle | 0.002 | 0.0-0.02 | yes | share of open-grass pixels brighter than median+0.15: tufts/debris must not read as sprinkles (region left of the stall) |
| value | far.saturation | 0.345 | 0.28-0.35 | yes | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | far.empty_share | 0.278 | 0.0-0.1 | **NO** | share of ~1.3 m blocks with almost no detail: The Guild never shows an empty patch (2026-10-16: 0.26) |
| value | far.key_fill | 2.124 | 3.0-5.0 | **NO** | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| info | mid masks | lit 41%, shadow 51% |  | yes | share of the frame classified lit / in shadow |
| value | mid.lit_mean | 0.476 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | mid.shadow_tint | 1.077 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | mid.contact_floor | 0.039 | 0.05-0.5 | **NO** | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | mid.whole_frame | 0.306 | 0.2-0.4 | yes | overall value structure |
| value | mid.shadow_green | 0.983 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | mid.saturation | 0.248 | 0.28-0.35 | **NO** | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | mid.key_fill | 4.008 | 3.0-5.0 | yes | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| value | night.mean | 0.135 | 0.1-0.2 | yes | night readable like Baldur's Gate 2 (~0.15), not black (user default) |
| value | night.torch_brightest | 2.128 | 1.8-10.0 | yes | mean of the torch's light pool / frame mean: the torch is clearly the brightest area at night |
| value | dusk.lit_mean | 0.238 | 0.3-0.5 | **NO** | whole frame at golden hour stays a daylight frame (review 5) |
| mipmaps | 61 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 44598 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 75086 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 89858 | <= 100000 | yes | triangles |
