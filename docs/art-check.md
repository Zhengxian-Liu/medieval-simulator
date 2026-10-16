# Art check

Result: **FAIL** (rerun: `python3 godot/tools/art_check.py`)

| check | item | measured | target | pass | note |
|---|---|---|---|---|---|
| value | far.lit_ground | 0.448 | 0.4-0.5 | yes | sunlit dirt in the square |
| value | far.shadow_ground | 0.220 | 0.2-0.26 | yes | ground in the barn's shadow: ~4:1 against lit |
| value | far.whole_frame | 0.314 | 0.25-0.4 | yes | overall value structure |
| value | far.footing_contact | 0.122 | 0.12-0.4 | yes | barn footing/skirt: contact dark but not black (review 3: 0.038, black halo) |
| value | far.shadow_tint | 1.013 | 0.85-1.0 | **NO** | neutral shadows: blue/red ratio in shadow (review 3: 1.11-1.28 = blue cast) |
| value | mid.lit_ground | 0.413 | 0.38-0.52 | yes | sunlit grass/path |
| value | mid.whole_frame | 0.250 | 0.2-0.4 | yes | overall value structure |
| value | mid.shadow_ground | 0.119 | 0.2-0.26 | **NO** | ground in shadow at mid zoom (art review 3 measured 0.12) |
| value | mid.shadow_tint | 1.283 | 0.85-1.0 | **NO** | neutral shadows at mid zoom |
| mipmaps | 61 textures | 0 without | 0 | yes | all mipmapped |
| budget | buildings/barn | 43846 | <= 100000 | yes | triangles |
| budget | buildings/cottage | 68278 | <= 100000 | yes | triangles |
| budget | buildings/longhouse | 83238 | <= 100000 | yes | triangles |
