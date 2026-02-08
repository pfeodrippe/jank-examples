# Fiction WASM Implementation Plan

**Date:** 2026-02-07

## Goal

Create `make fiction-wasm` that runs the Fiction narrative game in the browser using **WebGPU**, while keeping the existing `make fiction` desktop build using **Vulkan**. The game's jank source files should use a **common graphics abstraction** so they don't know which backend is active.

---

## Architecture

```
                    jank game code (.jank files)
                           |
                    requires "fiction_gfx.hpp"
                    (namespace fiction_engine + fiction)
                           |
              ┌────────────┴────────────┐
              |                         |
   fiction_gfx_vulkan.hpp    fiction_gfx_webgpu.hpp
   (desktop: Vulkan+SDL3)   (WASM: WebGPU+canvas)
```

### Key Insight

The jank files require C++ headers with `:scope` directives that set the C++ namespace. The game calls `engine/init`, `fiction/build_dialogue_from_pending`, etc. The common header `fiction_gfx.hpp` must expose the **same function names in the same namespaces** (`fiction_engine` and `fiction`). Which backend header it includes is controlled by a `#define`:

```cpp
// fiction_gfx.hpp
#ifdef FICTION_USE_WEBGPU
  #include "fiction_gfx_webgpu.hpp"
#else
  #include "fiction_gfx_vulkan.hpp"
#endif
```

For WASM builds, `FICTION_USE_WEBGPU` is defined via `--em-flag "-DFICTION_USE_WEBGPU"`.

### Why not runtime polymorphism?

- The project uses header-only inline functions (ODR-safe pattern with `inline` + static locals)
- No vtables, no dynamic dispatch, no shared libraries in WASM
- Compile-time `#ifdef` is simpler, faster, and matches the existing pattern

---

## File Layout

```
fiction_gfx/
  fiction_gfx.hpp              # Dispatcher: #ifdef → vulkan or webgpu
  fiction_gfx_vulkan.hpp       # Desktop backend (refactored from vulkan/fiction_engine.hpp + fiction_text.hpp)
  fiction_gfx_webgpu.hpp       # WASM backend (new)
  fiction_text_common.hpp      # Shared text layout logic (no GPU calls)
  stb_truetype.h               # symlink or copy from vulkan/
  stb_image.h                  # symlink or copy from vulkan/

fiction_wasm/                  # WGSL shaders
  text.wgsl
  bg.wgsl
  particle.wgsl

vulkan_fiction/                # Existing GLSL shaders (unchanged)
  text.vert, text.frag, ...
```

---

## API Surface (what both backends must provide)

### Namespace `fiction_engine` (from fiction_gfx.hpp)

| Function | Signature | Notes |
|----------|-----------|-------|
| `init` | `bool init(const char* title)` | Create window/canvas, init GPU |
| `cleanup` | `void cleanup()` | Destroy everything |
| `should_close` | `bool should_close()` | Return whether to exit |
| `poll_events` | `void poll_events()` | Fill event queue |
| `draw_frame` | `void draw_frame()` | Execute render callback, present |
| `set_render_callback` | `void set_render_callback(RenderCallback cb)` | Register frame callback |
| `get_screen_width` | `uint32_t get_screen_width()` | Framebuffer width |
| `get_screen_height` | `uint32_t get_screen_height()` | Framebuffer height |
| `get_pixel_scale` | `float get_pixel_scale()` | HiDPI scale factor |
| Event accessors | `get_event_count/type/scancode/scroll_y/mouse_x/mouse_y/mouse_button` | Indexed event queue |
| File I/O | `read_file_lines/get_file_line/get_file_mod_time` | Platform-independent |

### Namespace `fiction` (from fiction_gfx.hpp)

| Function | Signature | Notes |
|----------|-----------|-------|
| `init_text_renderer_simple` | `bool init_text_renderer_simple(float w, float h, const string& shaderDir)` | Init renderer |
| `load_background_image_simple` | `bool load_background_image_simple(const char* path, const string& shaderDir)` | Load bg |
| `cleanup_text_renderer` | `void cleanup_text_renderer()` | Cleanup |
| `text_renderer_initialized` | `bool text_renderer_initialized()` | Status |
| `clear_pending_entries` | `void clear_pending_entries()` | Clear frame data |
| `add_history_entry` | `void add_history_entry(int type, const char* speaker, const char* text, float r, float g, float b, bool selected)` | Add dialogue |
| `add_choice_entry_with_selected` | `void add_choice_entry_with_selected(const char* text, bool selected)` | Add choice |
| `build_dialogue_from_pending` | `void build_dialogue_from_pending()` | Build vertices |
| `set_panel_colors/position` | Style setters | Layout config |
| `update_mouse_position` | `void update_mouse_position(float x, float y)` | Input |
| `get_hovered_choice/get_clicked_choice` | `int get_*()` | Mouse interaction |
| `scroll_dialogue/scroll_to_bottom` | Scroll control | Scroll |
| `get_text_renderer` | `TextRenderer*& get_text_renderer()` | Direct struct access |
| Debug | `get_text_vertex_count/get_pending_history_count/get_pending_choices_count` | Debug info |

### Structs that must be identical (jank accesses fields via `cpp/.-`)

- `PanelStyle` — all 31 float fields (colors, positions, spacing)
- `TextRenderer` — fields: `style`, `autoScrollEnabled`, `scrollAnimationSpeed`
- `DialogueEntry`, `EntryType`, `TextVertex`, `GlyphInfo`, `FontAtlas`

These structs go in `fiction_text_common.hpp` (shared between backends).

---

## Shared Code Extraction

The current `fiction_text.hpp` (~2490 lines) is approximately:
- **~1200 lines of text layout logic** (word wrap, glyph rendering to vertex buffer, dialogue panel layout, scroll math, mouse hit testing) — **platform-independent**, writes to a `void*` mapped vertex buffer
- **~800 lines of Vulkan resource creation** (images, buffers, samplers, descriptors, pipelines, command recording)
- **~490 lines of shader management** (SPIR-V loading, pipeline creation, hot reload)

The text layout logic can be extracted into `fiction_text_common.hpp` and shared between both backends. Each backend only needs to provide:
1. GPU buffer/image creation
2. Shader pipeline creation  
3. Command recording / draw submission

---

## WebGPU Backend Design

### Init (`fiction_engine::init`)
```
- Use Emscripten's built-in WebGPU: #include <webgpu/webgpu.h>
- wgpuCreateInstance() (or navigator.gpu via JS glue)
- wgpuInstanceRequestAdapter() → wgpuAdapterRequestDevice()
- Get canvas via emscripten_get_element_css_size() + wgpuSurfaceConfigure()
- For events: emscripten_set_keydown_callback, emscripten_set_wheel_callback, emscripten_set_mousedown/up/move_callback
```

### Draw Frame (`fiction_engine::draw_frame`)
```
- wgpuSurfaceGetCurrentTexture()
- wgpuDeviceCreateCommandEncoder()
- wgpuCommandEncoderBeginRenderPass() with clearColor
- Call render callback with the render pass encoder
- wgpuRenderPassEncoderEnd()
- wgpuQueueSubmit()
```

### Text Renderer
```
- Font atlas: wgpuDeviceCreateTexture(R8Unorm) + wgpuQueueWriteTexture()
- Vertex buffer: wgpuDeviceCreateBuffer(VERTEX | COPY_DST) + wgpuQueueWriteBuffer()
- Bind groups instead of descriptor sets
- Render pipelines created from WGSL shaders (embedded as strings or loaded from file)
- Push constants → uniform buffers (WebGPU has no push constants)
```

### Main Loop
```
- WASM can't spin-loop. Two options:
  a) emscripten_set_main_loop() — standard approach
  b) -sASYNCIFY — lets synchronous code yield. Already used in integrated_wasm build.
- We'll use ASYNCIFY (matches existing pattern), so the main loop `(loop [] (when-not (should_close) ...))` works unmodified.
```

---

## Shader Conversion (GLSL → WGSL)

### text.wgsl (vertex + fragment combined)
- Push constant `vec2 screenSize` → uniform buffer binding
- `sampler2D fontAtlas` → separate `texture_2d<f32>` + `sampler`
- Fragment: sample `.r` channel as coverage, multiply with vertex alpha
- `discard` → `discard`

### bg.wgsl
- Same as text.wgsl but fragment samples RGBA texture
- No discard, no alpha test

### particle.wgsl
- Push constant `vec2 screenSize, float time, float padding` → uniform buffer
- Procedural SDF fragment shader (hash functions, smoothstep, etc.)
- All GLSL math functions exist in WGSL with same names
- `fract`, `sin`, `cos`, `dot`, `smoothstep`, `length`, `max`, `min`, `abs` — all available

---

## Build Pipeline

### New Makefile targets

```makefile
# Compile fiction_gfx C++ to WASM .o
build-fiction-gfx-wasm:
    em++ -std=c++20 -O2 -c -fPIC \
        -DFICTION_USE_WEBGPU -DJANK_TARGET_EMSCRIPTEN -DJANK_TARGET_WASM=1 \
        -DSTBI_NO_THREAD_LOCALS \
        -Ifiction_gfx -Ivendor -Ivendor/flecs/distr \
        fiction_gfx/fiction_gfx_impl.cpp \
        -o fiction_gfx/build-wasm/fiction_gfx_wasm.o

# Compile native stub for JIT symbol resolution during AOT
build-fiction-gfx-native:
    $(CXX) -std=c++20 -O2 -c -fPIC \
        -DFICTION_USE_WEBGPU \
        -I/opt/homebrew/include \
        -Ifiction_gfx -Ivendor -Ivendor/flecs/distr \
        fiction_gfx/fiction_gfx_stub.cpp \
        -o fiction_gfx/build/fiction_gfx_stub.o

# Build native .dylib for JIT
build-fiction-gfx-dylib:
    $(CXX) -std=c++20 -O2 -dynamiclib -Wl,-undefined,dynamic_lookup \
        -Ifiction_gfx -Ivendor \
        fiction_gfx/fiction_gfx_stub.cpp \
        -o fiction_gfx/libfiction_gfx_stub.dylib

# Main WASM target
fiction-wasm: build-flecs-wasm build-vybe-wasm build-fiction-gfx-wasm build-fiction-gfx-dylib
    ./bin/run_fiction_wasm.sh
```

### run_fiction_wasm.sh (new script, following integrated_wasm pattern)

```bash
RELEASE=1 ./bin/emscripten-bundle -v \
    --native-obj "vendor/flecs/distr/flecs.o" \
    --native-obj "vendor/vybe/vybe_flecs_jank.o" \
    --native-obj "fiction_gfx/build/fiction_gfx_stub.o" \
    --prelink-lib "vendor/flecs/distr/flecs_wasm.o" \
    --prelink-lib "vendor/vybe/vybe_flecs_jank_wasm.o" \
    --prelink-lib "fiction_gfx/build-wasm/fiction_gfx_wasm.o" \
    -I fiction_gfx \
    -I vendor \
    -I vendor/flecs/distr \
    --em-flag "-DFICTION_USE_WEBGPU" \
    --em-flag "-sASYNCIFY" \
    --em-flag "-sFORCE_FILESYSTEM=1" \
    --em-flag "-sALLOW_MEMORY_GROWTH=1" \
    --em-flag "-sINITIAL_MEMORY=67108864" \
    --em-flag "-sUSE_WEBGPU=1" \
    "$SOMETHING_DIR/src/fiction.jank"
```

### Critical: Dual Object Pattern

For the AOT phase, the native jank compiler needs to resolve C++ symbols. But the WebGPU code won't compile natively (no `<webgpu/webgpu.h>` on macOS). So we need a **native stub** that provides all the same function signatures as the WebGPU backend but with empty/dummy implementations:

```cpp
// fiction_gfx_stub.cpp - compiled natively for JIT symbol resolution
namespace fiction_engine {
    inline bool init(const char* title) { return false; }
    inline void cleanup() {}
    inline bool should_close() { return true; }
    // ... all 18 functions with dummy implementations
}
namespace fiction {
    // ... all 19 functions with dummy implementations
    // Structs: PanelStyle, TextRenderer, etc. must match exactly
}
```

---

## Changes to jank Source Files

### Minimal change: Only the header require path

**fiction.jank** line 12:
```diff
-["vulkan/fiction_engine.hpp" :as engine :scope "fiction_engine"]
+["fiction_gfx/fiction_gfx.hpp" :as engine :scope "fiction_engine"]
```

**fiction/render.jank** lines 8-9:
```diff
-["vulkan/fiction_engine.hpp" :as engine :scope "fiction_engine"]
-["vulkan/fiction_text.hpp" :as fiction :scope "fiction"]
+["fiction_gfx/fiction_gfx.hpp" :as engine :scope "fiction_engine"]
+["fiction_gfx/fiction_gfx.hpp" :as fiction :scope "fiction"]
```

Wait — both `engine` and `fiction` scopes come from the same include chain (`fiction_text.hpp` includes `fiction_engine.hpp`). The new `fiction_gfx.hpp` dispatches to either vulkan or webgpu backend, which in turn provides both namespaces.

**fiction/parser.jank** line 12:
```diff
-["vulkan/fiction_engine.hpp" :as engine :scope "fiction_engine"]
+["fiction_gfx/fiction_gfx.hpp" :as engine :scope "fiction_engine"]
```

**fiction/state.jank**: No changes.

### Desktop build also needs updating

The desktop `make fiction` / `run_fiction.sh` also needs to use the new header path. The Vulkan backend header (`fiction_gfx_vulkan.hpp`) will essentially be the existing `fiction_engine.hpp` + `fiction_text.hpp` wrapped in the `fiction_gfx` directory with the conditional include mechanism. Since `FICTION_USE_WEBGPU` is NOT defined for desktop builds, it picks the Vulkan backend automatically.

---

## Implementation Order

1. **Create `fiction_gfx/` directory structure**
2. **Extract shared code**: `fiction_text_common.hpp` (structs, text layout, vertex generation)
3. **Create `fiction_gfx_vulkan.hpp`**: Wrap existing Vulkan code, include shared text code
4. **Create `fiction_gfx.hpp`**: Conditional dispatcher
5. **Update jank files**: Change require paths (3 files, 4 lines)
6. **Update `run_fiction.sh`**: Add `-I fiction_gfx`
7. **Test desktop build** with `make fiction` — must work identically
8. **Create WGSL shaders** (3 files)
9. **Create `fiction_gfx_webgpu.hpp`**: WebGPU backend
10. **Create `fiction_gfx_stub.cpp`**: Native stub for AOT
11. **Create `fiction_gfx_impl.cpp`**: WASM compilation unit (includes webgpu header, defines STB_IMPLEMENTATION)
12. **Create `run_fiction_wasm.sh`**: Build script
13. **Update Makefile**: Add fiction-wasm targets
14. **Test WASM build** with `make fiction-wasm`

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| WebGPU API differences from Vulkan (no push constants, different binding model) | Use uniform buffers; WebGPU is conceptually similar to Vulkan |
| ASYNCIFY stack limits | Already proven to work in integrated_wasm |
| STB libraries in WASM | Pure C, no platform deps, confirmed working in raylib WASM build |
| Font file loading in WASM | Use Emscripten's virtual filesystem (`--preload-file fonts/`) |
| Shader hot-reload in WASM | Skip entirely (no `glslc` in browser); load pre-compiled WGSL |
| `get_file_mod_time` in WASM | Return 0 / stub out hot-reload (not useful in browser) |
| Canvas vs SDL window differences | WebGPU backend handles canvas directly, no SDL dependency in WASM |

---

## What to do next

Start implementing from step 1. Begin with the shared code extraction and Vulkan refactor to validate the abstraction layer works on desktop before touching WebGPU.
