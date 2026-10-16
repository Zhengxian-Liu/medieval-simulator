# Art check

Result: **FAIL** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| info | far masks | lit 60%, shadow 25%, key warmth 1.06 |  | yes | share of the frame classified lit / in shadow |
| value | far.lit_mean | 0.468 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | far.shadow_tint | 1.065 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | far.contact_floor | 0.122 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | far.whole_frame | 0.388 | 0.25-0.46 | yes | overall value structure of the far daylight frame. Max 0.40 -> 0.46, user decision 2026-10-16 (“改成 0.46”): 0.40 had no reference; The Guild daylight frames measure 0.449 (guild3_3) and 0.348 (guild3_1), and the cap blocked grass, footing, thatch brightening and the reed thatch texture (docs/art-check-conflicts.md) |
| value | far.shadow_green | 0.984 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | far.grass_speckle | 0.014 | 0.0-0.02 | yes | share of open-grass pixels brighter than median+0.15: tufts/debris must not read as sprinkles (region left of the stall) |
| value | far.saturation | 0.328 | 0.28-0.35 | yes | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | far.empty_share | 0.056 | 0.0-0.1 | yes | share of ~1.3 m blocks with almost no detail: The Guild never shows an empty patch (2026-10-16: 0.26) |
| value | far.key_fill | 4.560 | 3.0-5.0 | yes | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| value | far.thatch_vs_grass | 1.035 | 1.0-1.5 | yes | lit thatch at or above lit grass in value (sRGB, ID pass). Measured on docs/reference/the-guild/guild3_3.jpg: lit thatch 0.57, lit lawn 0.45-0.60 -> ~1.0-1.25 (Fable round 7 estimated 2.7; the reference measurement overrides it). Round 7: 0.59 |
| value | far.thatch_saturation | 0.387 | 0.3-0.6 | yes | lit thatch straw saturation within the reference range: measured guild3_3 0.40, guild3_1 0.60 (Fable round 7 estimated 0.29; reference measurement overrides it) |
| value | far.leaves_value | 0.292 | 0.235-0.5 | yes | lit foliage reads as green masses, not near-black holes: >= 60/255 (Fable round 7, bushes measured ~39/255). Set before tuning |
| value | far.cloth_value | 0.492 | 0.3-0.51 | yes | lit stall canvas / sacks no brighter than 130/255: the canvas was the brightest surface and pulled the eye off the well (Fable round 7). Set before tuning |
| value | far.thatch_bundles | 0.050 | 0.07-0.15 | **NO** | bundle-scale detail on lit thatch: sd of (blur 3 px - blur 12 px). The Guild measures 0.092 (guild3_3 250x100+300+110) and 0.093 (guild3_1 400x200+700+120) at the same course spacing as our far view (~33 px); round 8: 0.036 (smooth fur). Set before tuning |
| value | far.roof_macro | 0.098 | 0.06-1.0 | yes | 16 px-blurred std dev over lit thatch (ID pass; was a fixed rect on the barn roof, which the yard oak's shadow now covers, Fable r7). Target unchanged (review 5: lowering it to 0.05 was fitting the check) |
| value | far.foliage_clusters | 0.096 | 0.09-0.3 | yes | crowns read as lit clusters with shadowed gaps, not uniform confetti: 8 px-blurred sd over all leaf pixels. The Guild trees 0.095 (guild3_3 400x300+1500+0) and 0.112 (250x150+1000+0); round 10: 0.071. Set before tuning |
| value | far.grass_value | 0.481 | 0.44-0.56 | yes | lit grass value (sRGB, ID pass). The Guild lawn measured 0.44-0.54 (guild3_3 80x200+10+330, 200x100+1100+250, 300x120+1450+330). Set before tuning |
| value | far.grass_saturation | 0.320 | 0.3-0.4 | yes | lit grass HSL saturation. The Guild lawn 0.31-0.36 (same rects). Set before tuning |
| value | far.grass_green | 1.343 | 1.0-1.35 | yes | lit lawn hue, g/avg(r,b) (ID pass): The Guild 1.03-1.35 (guild3_3 300x120+1450+330, 80x200+10+330, 200x100+1100+250); olive, not lime (Fable r12: mid lawn 1.46). Set before tuning |
| info | mid masks | lit 40%, shadow 53%, key warmth 1.07 |  | yes | share of the frame classified lit / in shadow |
| value | mid.lit_mean | 0.444 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | mid.shadow_tint | 1.071 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | mid.contact_floor | 0.055 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | mid.whole_frame | 0.271 | 0.2-0.4 | yes | overall value structure |
| value | mid.shadow_green | 0.976 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | mid.saturation | 0.317 | 0.28-0.35 | yes | mean HSL saturation of sunlit pixels (AgX correctly desaturates shade, review 5); colour from materials, grade neutral |
| value | mid.key_fill | 10.098 | 3.0-5.0 | **NO** | light on sunlit vs shadowed surfaces (lighting-only pass, linear): ~4:1 key:fill. Replaces shadow_mean, which mixed albedo in: mid shadow pixels are dark soil (0.5x lit albedo), far ones 1.9x, so no single fill could satisfy both (2026-10-16 sweep) |
| value | mid.grass_green | 1.325 | 1.0-1.35 | yes | lit lawn hue, g/avg(r,b) (ID pass): The Guild 1.03-1.35 (guild3_3 300x120+1450+330, 80x200+10+330, 200x100+1100+250); olive, not lime (Fable r12: mid lawn 1.46). Set before tuning |
| value | mid.grass_saturation | 0.328 | 0.3-0.4 | yes | lit grass HSL saturation in the mid view: The Guild lawn 0.31-0.36 (same rects); the close lawn overshot to 0.445 (Fable r12). Set before tuning |
| value | night.mean | 0.110 | 0.1-0.2 | yes | night readable like Baldur's Gate 2 (~0.15), not black (user default) |
| value | night.torch_brightest | 2.254 | 1.8-10.0 | yes | mean of the torch's light pool / frame mean: the torch is clearly the brightest area at night |
| value | dusk.lit_mean | 0.266 | 0.22-0.5 | yes | whole frame at golden hour stays a daylight frame (review 5). 0.30 -> 0.22, user decision 2026-10-16: a 16-17 deg sun lights flat ground at ~0.29 of noon, so golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.key_warmth | 1.565 | 1.5-2.5 | yes | red/blue of the light on sunlit surfaces (lighting-only pass): noon sun [1,0.96,0.9] gives ~1.1; golden hour clearly warm (>= 1.5) but not sunset orange (<= 2.5). Set before measuring. user decision 2026-10-16 (“改看暖色”): golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.key_fill | 5.391 | 3.0-6.0 | yes | a low sun still models form: key:fill at least the daytime 3, up to 6 (long, deeper shadows). Set before measuring. user decision 2026-10-16 (“改看暖色”): golden hour is judged by warmth and contrast, not brightness (docs/art-check-conflicts.md) |
| value | dusk.shadow_tint | 1.084 | 0.87-1.3 | yes | blue/red of the light in shadow (lighting-only pass): at golden hour only the key is warm, the shade stays neutral or cool (Fable round 6: R/B <= 1.15) |
| value | dusk.saturation | 0.421 | 0.2-0.4 | **NO** | whole-frame HSL saturation: golden hour, not an orange filter (round 6 measured 0.48 vs 0.33 at noon) |
| value | morning.key_warmth | 1.094 | 0.95-1.15 | yes | red/blue of the key light on lit pixels: noon measures 1.05; a visibly orange morning would read > 1.15 |
| mipmaps | 70 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 50590 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 81162 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 94994 | <= 100000 | yes | triangles |
