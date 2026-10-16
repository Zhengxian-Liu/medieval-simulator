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

## The Guild (user reference, 2026-10-16)
Screenshots: `docs/reference/the-guild/` (public Steam store images, sources in `SOURCES.md`; internal reference only).
The Guild 3 (2020) is the closest match to what we want; The Guild II Renaissance (2010) shows the same layout ideas
with older rendering.

| aspect | The Guild | adopt? |
|---|---|---|
| camera | free camera; the typical town view is a high oblique ~35–50° down at 20–60 m, close-ups at house scale | yes: our 16 m far / 10 m mid range fits; keep the option of a wider town view later |
| scale & density | houses packed close along lanes, gardens and fences filling every gap, trees woven between buildings; never an empty square | **yes, main gap**: our village is sparse — more buildings, closer spacing, gardens, lanes |
| buildings | thatch is thick, shaggy, bundle-textured with ragged eaves; walls are timber, daub and rough stone; lots of small attachments (lean-tos, stairs, porches, shutters, signs) | yes: add lean-tos/porches/attachments to the building specs; our thatch already layered |
| ground | cobbles in the town, packed dirt and worn grass elsewhere; strong transitions; puddles and wheel ruts | yes: cobbled square/lane option, ruts |
| clutter | woodpiles, barrels, crates, carts, tools, fences, market goods at every door | yes: denser yard kits |
| water | river with jetties, boats, reeds, clear shallows | yes: jetty + reeds props |
| light & grade | bright midday sun, saturated greens, warm thatch; GI-soft shade | partly: keep our neutral AgX grade (user), but match the sun brightness and soft shade |
| people | townsfolk on every street | later (characters are not built yet) |
| what not to copy | UI, the 2010 game's flat textures and fake mountains, the city/cathedral scale | — |

How it ranks in reviews: Fable scores each round against both the pre-rendered indication and these frames
("how close to The Guild's feel").
