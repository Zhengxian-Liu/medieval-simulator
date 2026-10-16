# Art check

Result: **PASS** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| info | far masks | lit 58%, shadow 27% |  | yes | share of the frame classified lit / in shadow |
| value | far.lit_mean | 0.440 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | far.shadow_mean | 0.240 | 0.2-0.26 | yes | mean value of pixels in sun shadow (sun adds <3x, bounce only): ~4:1 linear against lit |
| value | far.shadow_tint | 1.044 | 0.85-1.05 | yes | blue/red in shadow: slightly cool skylight is natural; the fault was 1.1-1.3 (review 3) |
| value | far.contact_floor | 0.110 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | far.whole_frame | 0.383 | 0.25-0.4 | yes | overall value structure |
| info | mid masks | lit 34%, shadow 52% |  | yes | share of the frame classified lit / in shadow |
| value | mid.lit_mean | 0.489 | 0.4-0.5 | yes | mean value of sunlit pixels (sun adds >5x the sun-off frame) |
| value | mid.shadow_mean | 0.163 | 0.16-0.26 | yes | mid-frame shadow pixels are half the barn's shaded dark timber wall; shadowed ground there measures 0.18-0.20 (2026-10-16) |
| value | mid.shadow_tint | 0.931 | 0.85-1.05 | yes | blue/red in shadow: slightly cool skylight is natural; the fault was 1.1-1.3 (review 3) |
| value | mid.contact_floor | 0.051 | 0.05-0.5 | yes | 1st percentile of the frame blurred 8 px: contacts dark but never black halos (reference measures 0.05-0.07) |
| value | mid.whole_frame | 0.304 | 0.2-0.4 | yes | overall value structure |
| mipmaps | 61 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 44598 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 68278 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 82674 | <= 100000 | yes | triangles |
