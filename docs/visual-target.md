# Visual target

Goal: realistic assets presented like a classic pre-rendered isometric game (Age of Empires II, Diablo II, Pillars of Eternity).
References the user picked:
- Pine & Parcel — https://x.com/zavrenn/status/2110759896580858276 (Blender + three.js). Primary target for look.
- NoMagic asset factory — https://x.com/icebakbum/status/2109558373150765535 (Tripo + headless Blender). Target for the asset *pipeline*, not the look (it is heavy-stylized).

Breakdown below is from the Pine & Parcel video thumbnail only (one frame, 420×235 crop); re-check against the full video when possible.

## Pine & Parcel, what makes it read as "pre-rendered"
| Aspect | Observed | Our status (2026-10-16) |
|---|---|---|
| Camera | High oblique, roughly 45–55° down, narrow FOV, no visible perspective convergence | Same (45°, 12° FOV) |
| Light | Warm key light from upper left, soft shadows, strong contact darkening under rocks and buildings, sky fill lifts shadows slightly blue-green | Real-time sun with hard-ish shadows, weak contact shadows |
| Palette | Saturated mid-greens, warm ochre paths and timber, deep teal water; dark values are rich, not grey | Greener after colour pass, still flat and evenly lit |
| Ground | Never one texture: grass, dirt paths, sand at the shore, rocks half-buried, grass clumps breaking every edge | One tiling grass texture |
| Water | Clear shallow-to-deep gradient over a visible bed, white foam hugging rocks and shore | Gradient + foam band, no visible bed detail, no foam around rocks |
| Density | Every square metre has something: rocks, bushes, fences, crates, boat, lamp posts, steps | Sparse |
| Assets | Buildings with layered roofs, porches, stairs, beams, trims; each asset has wear and colour variation | Box-built cottages, clean |
| Edges | No hard line where a mesh meets the ground: everything is blended with clutter or AO | Hard intersections everywhere |

## What to change, in order of visual payoff
1. Lighting: baked indirect light + AO (Godot LightmapGI) so contact shadows and bounce light exist; keep the real-time sun for day/night.
2. Ground: blended grass / dirt / sand / rock layers with paths, plus grass clumps and pebbles along every edge.
3. Density: clutter kits around each house (woodpile, barrels, crates, tools, fences, carts).
4. Hero assets with real detail (irregular stones, bevelled beams, uneven thatch, wear masks) instead of box geometry.
5. Water: bed detail visible through shallow water, foam around rocks.

`docs/screenshots/cycles_far.png` is the same scene path-traced in Blender Cycles: it isolates how much of the gap is rendering (1) versus content (2–5).
