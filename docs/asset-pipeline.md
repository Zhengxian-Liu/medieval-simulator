# Asset pipeline (Tripo → Blender → UE)

Status: planned, not built. First test: one barrel and one axe, after `TRIPO_API_KEY` is set in the user's environment (never in git).

## Who makes what
- Main family characters: MetaHuman.
- Tripo: props, clothing pieces, animals, background villagers.
- Terrain, rocks, foliage, building materials: Megascans (Fab).

## Stages
Every hand-off between tools gets an automated check; bugs live between tools.

1. **Tripo** (API, CLI script)
   - Humanoids: 4-view A-pose reference sheet as input; head, body and hair generated as separate parts (~25k quads each).
   - Auto-rig with one pinned rig version shared by all humanoids.
2. **Blender, headless** (`blender --background --python ...`)
   - Inspect and report first: skeleton, weights, texture links, foot contact, scale/axis, poly count, animation range.
   - Then fix. Fix parameters live in CSV/JSON per asset, never hand edits.
   - Decimate to budget, LODs, `UCX_` collision, sockets; export FBX to `Art/Source/`.
3. **UE** (Python commandlet import)
   - Retarget humanoids to the Mannequin with the IK Retargeter; compare the rest pose to the original before touching animations.
   - Auto screenshots of each asset for the user to pick and sign off.
