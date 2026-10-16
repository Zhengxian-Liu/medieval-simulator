# Art check

Result: **PASS** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| info | far masks | lit 54%, shadow 27% |  | yes | share of the frame classified lit / in shadow |
| value | far.lit_mean | 0.411 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | far.shadow_mean | 0.258 | 0.2-0.26 | yes | mean value of pixels in sun shadow (sun adds <3x, bounce only): ~4:1 linear against lit |
| value | far.shadow_tint | 1.017 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | far.contact_floor | 0.067 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | far.whole_frame | 0.353 | 0.25-0.4 | yes | overall value structure |
| value | far.shadow_green | 0.993 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | far.roof_macro | 0.055 | 0.05-1.0 | yes | 16 px-blurred std dev on the barn roof: thatch shows bundle patches and eave ageing, not one flat plane (review 4: 0.035; 0.055 after the specular fix and bundle patches, visibly patchy) |
| value | far.grass_speckle | 0.004 | 0.0-0.02 | yes | share of open-grass pixels brighter than median+0.15: tufts/debris must not read as sprinkles (region left of the stall) |
| value | far.saturation | 0.330 | 0.28-0.35 | yes | mean HSL saturation: references sit at 0.30-0.40; colour comes from materials, the grade stays neutral (user default, 2026-10-16) |
| info | mid masks | lit 36%, shadow 53% |  | yes | share of the frame classified lit / in shadow |
| value | mid.lit_mean | 0.479 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | mid.shadow_mean | 0.204 | 0.2-0.26 | yes | restored to 0.20 (review 4: lowering it fitted the check to the frame) |
| value | mid.shadow_tint | 0.926 | 0.85-1.2 | yes | blue/red of the light in shadow (lighting-only pass): skylit shade slightly cool is natural; review 3's fault was a strong blue cast |
| value | mid.contact_floor | 0.051 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | mid.whole_frame | 0.317 | 0.2-0.4 | yes | overall value structure |
| value | mid.shadow_green | 0.968 | 0.95-1.05 | yes | g/avg(r,b) of the light in shadow (lighting-only pass): no magenta bounce off red walls (review 4) |
| value | mid.saturation | 0.314 | 0.28-0.35 | yes | mean HSL saturation: references sit at 0.30-0.40; colour comes from materials, the grade stays neutral (user default, 2026-10-16) |
| mipmaps | 61 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 44598 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 68278 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 82674 | <= 100000 | yes | triangles |
