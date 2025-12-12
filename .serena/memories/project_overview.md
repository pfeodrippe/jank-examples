# Project Overview

## Purpose
Integrated demo combining Raylib + ImGui + JoltPhysics + Flecs ECS using jank (Clojure-like language with C++ interop).

## Tech Stack
- **jank**: Clojure dialect with native C++ interop
- **Raylib**: Graphics/window library
- **ImGui**: Immediate mode GUI
- **JoltPhysics**: Physics engine
- **Flecs**: Entity Component System

## Key Files
- `src/my_integrated_demo.jank` - Main demo file
- `CLAUDE.md` - Project rules and instructions
- `ai/` - AI session logs (required to create after each session)

## Running
```bash
./bin/run_integrated.sh  # Run the demo
./bin/run_sdf.sh         # Run SDF Vulkan viewer
```

## jank C++ Interop Patterns
- `cpp/.-field` for field access
- `cpp/.method` for method calls
- `cpp/*` for pointer dereference
- `ptr->field` = `(cpp/.-field (cpp/* ptr))`
