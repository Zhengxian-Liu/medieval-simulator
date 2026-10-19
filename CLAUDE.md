# MedievalSim

Unreal Engine 5 C++ life-sim. Realistic 3D; camera zooms continuously from an isometric overview to third-person over-the-shoulder.

## Current front end: the browser text game — decided 2026-10-19
The user decided to make the game text-first: the browser version (`simcore/tools/ui/`, run `cd simcore && bash scripts/ui.sh`) is the real game. Work now goes into how text information is laid out and shown; images come later. Godot/3D art is paused (kept in git below for reference).

## Engine attempt: Godot 4.7 (`godot/`) — decided 2026-10-16, paused 2026-10-19
UE (`Source/`, `Content/`, `Config/`) and three.js (`web/`) attempts are kept in git for reference; new work goes in `godot/`. The playtest sim is `simcore/` (run: `cd simcore && bash scripts/ui.sh`).
- Forward+, Jolt physics. GDScript, statically typed. Logic that can be pure (e.g. `scripts/camera_zoom.gd`) stays free of nodes and is tested headless.
- **Art follows `docs/art-pipeline.md`.** Every look parameter lives in `godot/art/look.json` (with its reason); assets are built by `godot/tools/blender/build_asset.py`; `godot/tools/art_check.py` is the pass/fail gate.
- The village is built procedurally in code (`scripts/village.gd`, seeded RNG) so screenshots are deterministic.

Commands (from repo root):
```
godot --headless --path godot -s res://tests/test_camera_zoom.gd                        # tests, exit 1 on failure
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python godot/tools/blender/build_asset.py -- all   # rebuild every asset
python3 godot/tools/art_check.py                                                        # capture + measure + report, exit 1 on failure
godot --path godot                                                                     # play (wheel zoom, WASD, T = +1 hour)
godot --editor --path godot                                                            # editor
```

## Unreal attempt (kept for reference)

## Rules (Unreal attempt)
- Game logic in C++. Blueprints only for asset wiring (meshes, materials, tuning), never logic.
- Simulation core lives in plain C++ classes (`F`-prefixed, no UObject/Actor), e.g. `Source/MedievalSim/Public/SimClock.h`. Actors/Components only read the sim and present it.
- Content data (items, recipes, NPCs) in CSV/JSON under `Data/`, imported as DataTables. No hard-coded tables in C++.
- Sim logic is covered by Automation Tests in `Source/MedievalSim/Private/Tests/`, named `MedievalSim.Sim.*`.
- Binary assets go through Git LFS (see `.gitattributes`).

## Look (Unreal attempt)
Realistic (Megascans / Tripo props / MetaHuman), presented like a classic pre-rendered isometric game: Lumen GI, soft AO, neutral grade, no stylisation, no pixelation.
Camera (`CameraZoom.h`, `MSPlayerCharacter`): far zoom is a fixed 45° yaw / -30° pitch iso view with a 10° FOV (near-orthographic); zooming eases continuously to a 65° perspective over-the-shoulder view. Camera distance is derived from FOV so framing shrinks without pops.

## Commands (Unreal attempt)
`UE` = `/Users/Shared/Epic Games/UE_5.8` (must match `EngineAssociation` in `MedievalSim.uproject`).

Build editor target:
```
"$UE/Engine/Build/BatchFiles/Mac/Build.sh" MedievalSimEditor Mac Development "-Project=$PWD/MedievalSim.uproject" -WaitMutex
```

Run sim tests headless; results go to `~/Library/Logs/Unreal Engine/MedievalSimEditor/MedievalSim.log` (grep `Test Completed`, `TEST COMPLETE. EXIT CODE`):
```
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/MedievalSim.uproject" -ExecCmds="Automation RunTests MedievalSim.Sim; Quit" -unattended -nullrhi -nosplash -log
```

Rebuild test scene (`/Game/Maps/TestVillage`, placeholder shapes):
```
"$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$PWD/MedievalSim.uproject" -run=pythonscript -script="$PWD/Tools/build_test_scene.py" -unattended
```

Visual check: far-iso and close-third-person screenshots into `Saved/Screenshots/`, then quits. Needs Xcode's Metal Toolchain (`xcodebuild -downloadComponent MetalToolchain`):
```
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/MedievalSim.uproject" -game -CaptureShots -windowed -ResX=1920 -ResY=1080 -unattended
```

## Web version (`web/`, three.js) — candidate replacement for the UE project
Decision 2026-10-16: moving toward Blender + three.js (lighter iteration); camera zooms from iso overview to mid range, not over-the-shoulder. UE files stay until the switch is confirmed.
- Camera logic: `web/src/cameraZoom.ts` (pure, tested in `web/e2e/camera-zoom.spec.ts`).
- Run: `cd web && npm run dev`. E2E + screenshots (`docs/screenshots/web_far.png`, `web_mid.png`): `cd web && npx playwright test`.
