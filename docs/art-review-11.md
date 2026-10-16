# Art review, round 11 (Fable, 2026-10-17)

Overall **6.8** (r10 6.7). Bar not met.

| area | r11 | r10 |
|---|---|---|
| lighting (day) | 7 | 7 |
| time of day | 6.5 | 7 |
| materials (buildings) | 7 | 7 |
| thatch | 6.5 | 6.5 |
| ground | 6.5 | 6 |
| foliage | 6 | 6 |
| density / composition | 6.5 | 6.5 |
| The Guild feel | 6.5 | 6.5 |

Improved: river streaks gone; grass->dirt edges clumped. Stricter read: 9:00 looks like noon (same shadow direction and
length); river is now an opaque slab without reflections; sacks/pots are visible hexagonal prisms.

Why shader work stalled (measured): thatch_bundles Guild 0.088-0.092, ours 0.046-0.055, the thatch scan alone 0.018:
the texture supplies no bundle detail, so course-scale shading reads as shingles. Foliage: one flat unshaded leaf per
card cannot make lit clusters with shadowed gaps.
Asset-level fixes: thatch - high-contrast straw tile with laid-bundle rows (CC0, else baked from strand cards in
Blender), tint near 1.0, eave-fringe strand cards (0.5-1 day). Foliage - baked 4-6 clump atlas (AO + underside
darkening), 60-120 cluster cards per crown (1-1.5 days).
Next: 1 whole_frame cap -> 0.45 then brighter grass (blocked on the user); 2 thatch texture + eave fringe; 3 foliage
cluster cards; then morning sun direction, water reflection, smooth faceted props.
