# Art review, round 8 (Fable, 2026-10-16)

Overall **6.7** (r7 6.6). Bar not met; foliage (5) and ground (6) gate it.

| area | r8 | r7 |
|---|---|---|
| lighting (day) | 7 | 7 |
| time of day | 7 | 7 |
| materials (buildings) | 7 | 6.5 |
| thatch | 6.5 | 6 |
| ground | 6 | 6.5 |
| foliage | 5 | 5 |
| density / composition | 6.5 | 6.5 |
| The Guild feel | 6.5 | 6 |

Improved: thatch reads as straw (lit 0.23 -> 0.38); gable bays readable; night hexagon gone; burlap cloth right.
Regressed / sideways: grass darker and flat (lit 0.43, sat 0.26-0.29, macro 0.01-0.04 vs reference lawn 0.52-0.68,
0.35-0.40, 0.11-0.13); thatch_vs_grass passed by darkening the denominator. Barn bush a smooth shaded sphere (macro
0.042 vs reference shrub 0.15); leaves_value samples lit oak tops, not the bush. Thatch bundle structure unchanged
(band-pass sd 0.022 vs 0.050). Cloth too uniform. River untouched.

Next by payoff (reference-measured targets, rects in the review transcript):
1 thatch bundle structure (band-pass 3-12 px sd >= 0.045, course gradient >= 0.09);
2 re-derive whole_frame from references (guild3_3 0.449, guild3_1 0.448) to 0.40-0.48 and brighten grass + thatch
  together (lit grass 0.50-0.60, sat 0.33-0.40) - fixes mid.saturation;
3 grass macro wear (open grass sd >= 0.10, macro >= 0.08);
4 foliage as shaded clusters (crown macro >= 0.08, dark share 0.15-0.30), bush clearance;
5 lighter packed earth at footings, braces, canvas stains, door clutter.
