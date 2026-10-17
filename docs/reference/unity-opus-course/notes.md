# Notes: "Build your own Game in Unity with Claude Opus 5.5" (rewind, 2026-10-14)

Summary in our words; source and images in `SOURCES.md`.

## What the article covers
- Two showcase builds and what was behind them: a cyberpunk shooter (models from a 3D generator, city in Blender, sound
  from a text-to-audio service, Unity for the game; guided step by step by its creator) and an MMO left running ~36 h
  with ~10 parallel agents after hours of preparation (references, a large brief, skills). Both openly unfinished in
  places; the author's point is that briefs, review and judgment stay with the person.
- Three ways to connect Claude to Unity (official plugin + unity CLI, Unity's MCP in the AI Assistant package, a
  community tool); the course uses the official plugin.
- Safety: commit before every larger request, keep approvals on, fix compile errors first (Unity safe mode blocks the
  connection), treat Blender's MCP server as able to run arbitrary code.
- Briefing habits: one system per prompt, a finish line, a "don't change" list, a report of every file touched, stop
  instead of silent workarounds, proof instead of claims. Keep context short; restart between tasks.
- A six-gate route for a small game (greybox + player, rules + a test, two enemy types with telegraphed attacks,
  seeded runs, models/light/sound, build), committing at each gate.
- Testing in four layers, including proof that Play mode is really advancing (an unfocused editor can freeze on its
  first frame while reporting "playing"), an autopilot that plays full runs with the real controls and lists bugs
  before any fix, and PlayMode tests with unambiguous exit codes. Bug reports carry the seed.
- Cost figures for the two builds (token use, generator credits) and the rule for parallel agents: different files
  per agent, since a scene is one file.

## Useful for this project
- **Frozen-frame trap = our capture hang.** Their unfocused-editor freeze is the same class of problem as our macOS
  covered-window stalls and GPU fence timeouts; their fix (prove the frame counter moves before trusting a capture) is
  a check `tools/capture.sh` could add on top of "every pass exists".
- **Lighting on a short leash with before/after captures** (their city went lavender from bloom over five rounds; a
  recompile silently wiped post-processing) matches our `tools/ab.py` rule; also: commit visual settings once accepted.
- **Blender named empties as placement markers** exported with real-metre layout: a possible route for
  `art/village.json` content (props, trees) authored in Blender instead of hand-typed coordinates.
- **Autopilot playtest + seeded runs** apply to simcore (it already replays from logs) and later to the playable village.
- **Generated-asset hygiene:** verify imported files are what their extension claims and check for clipping (their
  audio case); same spirit as our reimport-before-capture lesson.
- **Parallel agents on separate files only** - consistent with how we split work.
