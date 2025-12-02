# ImGui Integration with jank + raylib

**Date**: 2024-12-01

## What I Learned

### 1. jank Supports C++ Natively
The user emphasized: "Do you really need C wrappers?? Jank supports cpp natively!!"

This means we can use C++ libraries directly via `cpp/raw` blocks without needing to create C wrapper layers. This is a significant advantage for integrating C++ libraries like ImGui.

### 2. rlImGui Crashes with raylib dylib
When trying to use rlImGui (the raylib ImGui integration library), it crashed during `rlImGuiSetup()`. The crash occurred even though:
- Raylib functions worked fine
- ImGui context creation worked fine
- The issue was specifically in rlImGui's initialization

**Root cause hypothesis**: ABI mismatch between the rlImGui object files and the raylib dylib, or internal state management issues.

### 3. Custom rlgl Renderer Solution
Instead of using rlImGui, we created a custom ImGui renderer using raylib's low-level `rlgl` API:

- `rlDrawRenderBatchActive()` - Flush render batch
- `rlDisableBackfaceCulling()` / `rlEnableBackfaceCulling()` - Culling control
- `rlSetTexture()`, `rlBegin()`, `rlEnd()` - Texture and primitive rendering
- `rlVertex2f()`, `rlTexCoord2f()`, `rlColor4ub()` - Vertex attributes
- `BeginScissorMode()` / `EndScissorMode()` - Clipping rectangles

### 4. WASM Build Requirements
For WASM builds with emscripten-bundle:
- **Native dylib needed for JIT**: During AOT compilation, jank's JIT needs to resolve symbols, requiring a native dylib
- **WASM objects for final linking**: Separate WASM-compiled object files are linked into the final bundle
- **Library naming convention**: `--native-lib imgui` looks for `imgui.dylib` or `libimgui.dylib` in `-L` paths

### 5. emscripten-bundle Architecture
```
Native dylib (libimgui.dylib) ─────> JIT symbol resolution during AOT
                                            │
                                            v
jank source ──> C++ codegen ──> WASM object files
                                            │
                                            v
WASM objects (imgui.o, etc.) ─────> Final em++ linking ──> .wasm/.js/.html
```

## What I Did

### 1. Created ImGui Submodule Integration
- Added `vendor/imgui` submodule
- Added `vendor/rlImGui` submodule (ultimately not used due to crashes)

### 2. Built Custom ImGui Renderer (`src/my_imgui_static.jank`)
A complete ImGui demo using raylib's rlgl API:
- Font texture creation and upload
- Triangle-based rendering of ImGui draw lists
- Mouse input handling (position, buttons, wheel)
- Keyboard input handling (modifiers, text input)
- Demo window, custom windows, widgets (sliders, buttons, checkboxes, color picker)

### 3. Created Build Infrastructure

**`build_imgui.sh`**:
- Compiles ImGui core files (imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp, imgui_demo.cpp)
- Supports both native (`clang++`) and WASM (`em++`) builds via `BUILD_WASM` flag
- Creates `libimgui.dylib` for native builds (needed for JIT)
- Uses `-fPIC` for native builds to enable shared library creation

**`run_imgui.sh`**:
- Native execution script
- Links ImGui objects and raylib dylib
- Runs via jank with proper include paths

**`run_imgui_wasm.sh`**:
- WASM build script using emscripten-bundle
- Uses `--native-lib imgui` for JIT symbol resolution
- Uses `--prelink-lib` for WASM object files
- Sets emscripten flags: `-sUSE_GLFW=3`, `-sASYNCIFY`, `-sFORCE_FILESYSTEM=1`

### 4. Resolved Library Path Issues
- Copied `libimgui.dylib` to `vendor/raylib/distr/` alongside raylib
- Created symlink `imgui.dylib -> libimgui.dylib` for consistent naming

## File Structure

```
vendor/
├── imgui/
│   ├── build/           # Native objects + dylib
│   │   ├── imgui.o
│   │   ├── imgui_draw.o
│   │   ├── imgui_tables.o
│   │   ├── imgui_widgets.o
│   │   ├── imgui_demo.o
│   │   └── libimgui.dylib
│   └── build-wasm/      # WASM objects
│       ├── imgui.o
│       ├── imgui_draw.o
│       ├── imgui_tables.o
│       ├── imgui_widgets.o
│       └── imgui_demo.o
├── raylib/
│   └── distr/
│       ├── libraylib_jank.dylib
│       ├── libimgui.dylib      # Copied for JIT resolution
│       └── imgui.dylib         # Symlink
└── rlImGui/             # Not used (crashes)

src/
└── my_imgui_static.jank  # ImGui demo with custom rlgl renderer

build_imgui.sh           # Build script for native/WASM
run_imgui.sh             # Native run script
run_imgui_wasm.sh        # WASM build script
```

## Build Sizes

- **Native dylib**: ~1 MB (`libimgui.dylib`)
- **WASM bundle**: ~18.7 MB (`my_imgui_static.wasm`)
- **JS loader**: ~213 KB (`my_imgui_static.js`)

## What's Next / Future Work

### 1. Investigate rlImGui Crash
Could try:
- Building rlImGui as part of the raylib dylib
- Debugging the specific crash location
- Checking ABI compatibility between raylib and rlImGui

### 2. Optimize WASM Size
- Enable more aggressive optimization flags
- Strip unused ImGui features (demo window, etc.)
- Consider splitting into smaller modules

### 3. Add Keyboard Navigation
Current implementation handles:
- Mouse input (position, buttons, wheel)
- Modifier keys (Ctrl, Shift, Alt, Super)
- Text input

Missing:
- Arrow key navigation
- Tab navigation
- Enter/Escape handling
- Full keyboard mapping for ImGui

### 4. Create Reusable ImGui Module
Extract the custom renderer into a reusable jank module that can be imported by other projects.

### 5. Explore ImGui Docking Branch
The current implementation uses ImGui master. Could explore the docking branch for multi-window support.

## Commands Reference

```bash
# Build ImGui native
./build_imgui.sh

# Build ImGui WASM
BUILD_WASM=1 ./build_imgui.sh

# Run native demo
./run_imgui.sh

# Build WASM demo
./run_imgui_wasm.sh

# Serve WASM demo
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm
python3 -m http.server 8888
# Open: http://localhost:8888/my_imgui_static_canvas.html
```

## WASM Browser Setup

For raylib/GLFW WASM apps, the HTML must:
1. Have a `<canvas id="canvas">` element
2. Set up `window.Module = { canvas: document.getElementById('canvas') }` BEFORE loading the ES6 module
3. Pass `window.Module` to the module factory: `initFn(window.Module)`
4. NOT use `-sINVOKE_RUN=0` - let main() run automatically during initialization

See `my_imgui_static_canvas.html` for the working pattern.

## Key Insight

The pattern for integrating C++ libraries with jank for both native and WASM:

1. **Native build**: Create dylib with `-fPIC` for JIT symbol resolution
2. **WASM build**: Compile separate WASM objects with `em++`
3. **emscripten-bundle**: Use `--native-lib` for JIT + `--prelink-lib` for WASM linking
4. **jank source**: Use `cpp/raw` blocks to directly use C++ APIs
