# Art review, round 6 (Fable, 2026-10-16)

Overall **6.5** (round 5: 6.8). Bar (overall >= 8, no area < 7) not met.

| area | r6 | r5 | note |
|---|---|---|---|
| lighting (day) | 7 | 6 | soft sun reads pre-rendered; mid shade crushed (shaded barn wall one dark plane) |
| time of day | 6 | 6 | morning neutral, night readable; dusk became a monochrome orange filter (frame sat 0.48 vs 0.33 noon) |
| materials (buildings) | 5 | 6 | **regression**: grey_plaster_03 x warm tint = tan slab; frame and infill same value |
| thatch | 5 | 5 | brushed fur; courses barely visible; too dark/orange vs Guild pale straw |
| ground | 6.5 | 6 | dirt good; grass one tile, no edge wear |
| foliage | 4 | 4 | no trees/bushes in the far frame |
| density/composition | 5.5 | 5.5 | square an empty disc |
| The Guild feel | 5 | 5 | lanes far apart, no canopy/fences/door clutter |

A/B: brush 0.12 **reject** (invisible: 0.75/255 mean diff); focal falloff **reject** (Gaussian blur, contradicts realistic);
vertex AO 0.7 **adopt** (needed for contact floor, not sufficient).

Top changes: 1 readable daub + dark frame (timber/plaster lit value <= 0.65); 2 thatch as pale straw bundles, deeper
course shadow, geometric course steps; 3 small oaks/bushes in the hero frame; 4 fill the square (well/cross, stalls,
fences, door clutter); 5 dusk: warm key only, neutral shade (dusk shade R/B <= 1.15, frame sat <= 0.40).
Also: night torch-base dark hexagon (cause unconfirmed), teal night fill, river smeared highlights, consider 45 deg pitch.
