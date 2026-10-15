# MedievalSim

Unreal Engine 5 C++ life-sim. Realistic 3D; camera zooms continuously from an isometric overview to third-person over-the-shoulder.

## Rules
- Game logic in C++. Blueprints only for asset wiring (meshes, materials, tuning), never logic.
- Simulation core lives in plain C++ classes (`F`-prefixed, no UObject/Actor), e.g. `Source/MedievalSim/Public/SimClock.h`. Actors/Components only read the sim and present it.
- Content data (items, recipes, NPCs) in CSV/JSON under `Data/`, imported as DataTables. No hard-coded tables in C++.
- Sim logic is covered by Automation Tests in `Source/MedievalSim/Private/Tests/`, named `MedievalSim.Sim.*`.
- Binary assets go through Git LFS (see `.gitattributes`).

## Commands (Mac, Apple Silicon)
Engine: `/Users/Shared/Epic Games/UE_5.5` (update `EngineAssociation` in `MedievalSim.uproject` and these paths when the engine changes).

Build editor target:
```
"/Users/Shared/Epic Games/UE_5.5/Engine/Build/BatchFiles/Mac/Build.sh" MedievalSimEditor Mac Development "-Project=$PWD/MedievalSim.uproject" -WaitMutex
```

Run sim tests headless:
```
"/Users/Shared/Epic Games/UE_5.5/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PWD/MedievalSim.uproject" -ExecCmds="Automation RunTests MedievalSim.Sim; Quit" -unattended -nullrhi -nosplash -log
```

Status: neither command has succeeded yet. UE 5.5 accepts only Xcode 15.2–16.9 and this Mac has Xcode 27.
