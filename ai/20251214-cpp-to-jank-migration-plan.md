# C++ to jank Migration Plan

## Phase 1 Status: COMPLETED

Event loop has been restructured! Events are now polled and dispatched in jank.

**What was done:**
1. Created `EventData` struct and `g_event_buffer` in C++
2. Created `poll_events_only()` that polls SDL events into buffer without dispatching
3. Created event accessor functions (`get_event_count`, `get_event_type`, etc.)
4. Created `src/vybe/sdf/events.jank` with:
   - Event constants (EVENT_QUIT, EVENT_KEY_DOWN, etc.)
   - `handle-scroll!` - fully in jank (camera zoom)
   - `handle-key-down!` - dispatch in jank, calls C++ helpers
   - `handle-mouse-button!` - delegates to C++ (raycast_gizmo complex)
   - `handle-mouse-motion!` - delegates to C++ (raycast_gizmo complex)
   - `poll-and-process-events!` - main event loop in jank
5. Updated `sdf.jank` to use `poll-events-jank!` instead of `poll-events!`

**New C++ helpers added:**
- `toggle_edit_mode()` - toggles edit mode
- `select_object_by_id(int id)` - selects object by ID
- `request_undo()`, `request_redo()`, `request_duplicate()`, `request_delete()`, `request_reset_transform()` - request flags

---

## Phase 2 Status: COMPLETED

Cleaned up dead C++ code that is no longer needed since event handling is in jank.

**Dead code removed from C++:**
- `handle_scroll()` - now in jank as `handle-scroll!`
- `handle_key_down()` - now in jank as `handle-key-down!`
- `process_event()` - event dispatch now in jank
- `poll_events()` - replaced by `poll_events_only()` + jank dispatch

**Dead code removed from jank:**
- `poll-events!` in render.jank - unused legacy wrapper

**New C++ helpers added:**
- `key_code_left()`, `key_code_right()` - arrow key codes
- `set_pending_shader_switch(int direction)` - set shader switch request

**C++ code reduction:** ~110 lines removed from sdf_engine.hpp

**Bug fix:** Added arrow key support for shader switching (was using SDLK_LEFT/RIGHT in old C++ code, now handled in jank)

---

## Phase 3 Status: COMPLETED (Shader Index Management)

Moved shader index management from C++ to jank.

**New C++ APIs added:**
- `get_shader_count()` - returns number of shaders
- `get_shader_name_at(idx)` - returns shader name at index
- `get_current_shader_index()` - returns current shader index
- `load_shader_at_index(idx)` - loads shader at specific index

**Moved to jank:**
- `switch-shader!` now does index wrap-around logic in jank, calls `load_shader_at_index`

**Dead code removed:**
- `switch_shader(int direction)` - logic now in jank

---

## Phase 4 Status: COMPLETED (Shader Auto-Reload)

Moved shader auto-reload check from C++ to jank.

**New C++ APIs added:**
- `get_shader_dir()` - returns shader directory path
- `get_file_mod_time(path)` - returns file modification time
- `get_last_shader_mod_time()` / `set_last_shader_mod_time(t)` - track last shader mod time

**Moved to jank:**
- `check-shader-reload!` in render.jank - checks if shader file changed and calls `reload_shader`
- Called from main loop in sdf.jank

**Dead code removed:**
- `check_shader_reload()` - logic now in jank
- Removed call from `update_uniforms()`

**Note:** Mouse handlers (`handle_mouse_button`, `handle_mouse_motion`) remain in C++ because they contain complex 3D math for gizmo interaction (raycast_gizmo, camera basis vectors, projection math). These could potentially be moved to jank but would require significant math library support.

---

## Phase 5 Status: COMPLETED (Shader Reload to Jank)

Fully moved shader reload logic to jank by creating a new `shader.jank` namespace to break circular dependencies.

**New namespace created:**
- `src/vybe/sdf/shader.jank` - centralized shader management

**Functions moved to shader.jank:**
- `reload-shader!` - reads shader file via C++ `read_text_file`, calls `compile_and_recreate_pipeline`
- `check-shader-reload!` - checks file modification time and calls `reload-shader!`
- `switch-shader!` - index management and wrap-around logic
- All shader getter/setter functions

**C++ functions kept:**
- `compile_and_recreate_pipeline(glsl_source, shader_name)` - compiles GLSL and recreates Vulkan pipeline
- `read_text_file(path)` - reads file contents (used instead of jank's buggy `slurp`)

**C++ functions removed:**
- `reload_shader()` - logic now in shader.jank

**Architecture:**
```
All shader operations now go through shader.jank:
  events.jank -> shader/reload-shader! -> sdfx/read_text_file + sdfx/compile_and_recreate_pipeline
  sdf.jank    -> shader/check-shader-reload! -> shader/reload-shader!
  sdf.jank    -> shader/switch-shader! -> sdfx/load_shader_at_index
```

**Bug workaround:** jank's `slurp` function has a bug with larger files (causes segfault). Used C++ `read_text_file` instead.

---

## Current State Analysis

### Function Call Graph (Internal C++ Dependencies)

```
poll_events (called from jank)
  └── process_event
        ├── handle_key_down
        │     ├── reload_shader (on 'R' key)
        │     └── select_object (on 1-9 keys)
        ├── handle_mouse_button
        │     └── raycast_gizmo
        ├── handle_mouse_motion
        │     └── raycast_gizmo
        └── handle_scroll

check_shader_reload (called from poll_events)
  └── reload_shader

switch_shader (called from jank)
  ├── scan_shaders
  └── load_shader_by_name
        └── read_file

reload_shader
  ├── read_text_file
  ├── compile_glsl_to_spirv
  └── vkCreate* functions

init (called from jank)
  ├── scan_shaders
  ├── read_file
  ├── compile_glsl_to_spirv
  └── ... Vulkan initialization

draw_frame (called from jank)
  └── ... Vulkan rendering commands
```

### Functions Called from jank (via sdfx/)

| Function | Has Internal C++ Callers? | Can Move to jank? |
|----------|---------------------------|-------------------|
| `init` | No | Complex - keep in C++ |
| `cleanup` | No | Complex - keep in C++ |
| `poll_events` | No | **YES - if we restructure** |
| `update_uniforms` | No | Possible |
| `draw_frame` | No | Complex - keep in C++ |
| `should_close` | No | **YES - simple getter** |
| `switch_shader` | No | **YES - if helpers exist** |
| `set_dirty` | No | **YES - simple setter** |
| `set_continuous_mode` | No | **YES - simple setter** |
| All `get_*`/`set_*` | No | Engine field access - keep in C++ |

### Functions with Internal C++ Callers (CANNOT move directly)

| Function | Called By |
|----------|-----------|
| `process_event` | `poll_events` |
| `handle_key_down` | `process_event` |
| `handle_mouse_button` | `process_event` |
| `handle_mouse_motion` | `process_event` |
| `handle_scroll` | `process_event` |
| `raycast_gizmo` | `handle_mouse_button`, `handle_mouse_motion` |
| `select_object` | `handle_key_down` |
| `reload_shader` | `handle_key_down`, `check_shader_reload` |
| `check_shader_reload` | `poll_events` |
| `scan_shaders` | `switch_shader`, `init` |
| `load_shader_by_name` | `switch_shader` |
| `read_file` | `load_shader_by_name`, `init` |
| `read_text_file` | `reload_shader`, `init` |
| `compile_glsl_to_spirv` | `reload_shader`, `init` |
| `find_memory_type` | `init`, `create_screenshot_buffer` |

## Migration Strategy

### The Key Insight

The main blocker is that `poll_events` calls `process_event` which calls all the event handlers. To move event handling to jank, we need to **restructure the event loop**.

### Phase 1: Restructure Event Loop (Required First)

**Current architecture:**
```
jank: (poll-events!)
  -> C++: poll_events()
    -> C++: process_event(event)
      -> C++: handle_key_down(), handle_mouse_*(), etc.
```

**Target architecture:**
```
jank: (poll-events!) ; just polls SDL, doesn't process
jank: (process-events!) ; jank handles event dispatch
  -> jank: (handle-key-down! key)
    -> C++: helpers for reload_shader, select_object, etc.
  -> jank: (handle-mouse-motion! ...)
    -> C++: raycast_gizmo (complex 3D math)
```

**Steps:**
1. Create `poll_events_raw()` that only polls SDL events, doesn't process them
2. Create `get_next_event()` or expose SDL event data to jank
3. Move event dispatch logic (`process_event`) to jank
4. Keep complex helpers in C++ (`raycast_gizmo`, `reload_shader`)

### Phase 2: Move Event Handlers to jank

After Phase 1, we can move these one by one:

1. **`handle_scroll`** - Simple, just updates camera
   - Dependencies: Engine camera fields
   - Requires: Camera setters (already exist)

2. **`handle_key_down`** - Key dispatch logic
   - Dependencies: `reload_shader`, `select_object`
   - Requires: Keep `reload_shader`, `select_object` as C++ helpers

3. **`handle_mouse_button`** - Click handling
   - Dependencies: `raycast_gizmo`, Engine state
   - Requires: Keep `raycast_gizmo` in C++ (complex 3D math)

4. **`handle_mouse_motion`** - Drag handling
   - Dependencies: `raycast_gizmo`, Engine state
   - Requires: Keep `raycast_gizmo` in C++

### Phase 3: Move Shader Operations

1. **`reload_shader`** - Complex
   - Keep `compile_glsl_to_spirv` in C++ (uses shaderc)
   - Keep `read_text_file` in C++ (or use jank slurp)
   - Move orchestration to jank
   - Requires: `compile_shader_to_spirv()` helper, `create_shader_module()` helper

2. **`switch_shader`** - Can move
   - Keep `scan_shaders` in C++ (directory iteration)
   - Keep `load_shader_by_name` in C++ (complex pipeline creation)
   - Move the index management to jank

3. **`check_shader_reload`** - Can move
   - Just stat() + conditional reload_shader call
   - Requires: File stat helper or keep in C++

## Recommended Migration Order

### Step 1: Event Loop Restructure
```
1.1 Create poll_events_only() - just SDL_PollEvent loop, stores events
1.2 Create get_pending_events() - returns event count
1.3 Create get_event_type(idx) - returns event type
1.4 Create event data accessors (get_event_key, get_event_mouse_x, etc.)
1.5 Move event dispatch to jank (process-events!)
1.6 Remove process_event from poll_events, keep as internal helper
```

### Step 2: Simple Handlers
```
2.1 Move handle_scroll to jank (uses camera setters)
2.2 Move handle_key_down dispatch to jank (keeps calling C++ helpers)
```

### Step 3: Complex Handlers
```
3.1 Move handle_mouse_button to jank (keeps raycast_gizmo in C++)
3.2 Move handle_mouse_motion to jank (keeps raycast_gizmo in C++)
```

### Step 4: Shader Operations
```
4.1 Create compile_current_shader_to_spirv() helper
4.2 Create recreate_compute_pipeline() helper
4.3 Move reload_shader orchestration to jank
4.4 Move check_shader_reload to jank
```

## C++ Helpers That Must Stay

These functions should remain in C++ due to complexity or jank limitations:

| Function | Reason |
|----------|--------|
| `raycast_gizmo` | Complex 3D math, matrix operations |
| `compile_glsl_to_spirv` | Uses shaderc C++ library |
| `find_memory_type` | Bitwise operations on native types |
| `read_file` / `read_text_file` | std::ifstream, could use jank slurp instead |
| `scan_shaders` | Directory iteration with opendir/readdir |
| `init` | Complex Vulkan initialization (1000+ lines) |
| `draw_frame` | Complex Vulkan rendering |
| `cleanup` | Complex Vulkan cleanup |
| All `get_*`/`set_*` | Engine struct field access |

## New C++ Helpers to Create

For the migration to work, we need these new helpers:

```cpp
// Event system
struct SDLEventData {
    uint32_t type;
    // Key event data
    int32_t key_code;
    // Mouse event data
    float mouse_x, mouse_y;
    float mouse_xrel, mouse_yrel;
    uint8_t mouse_button;
    // Scroll data
    float scroll_x, scroll_y;
};

inline int poll_events_to_buffer();  // Returns number of events
inline SDLEventData get_event(int idx);

// Shader helpers (for Phase 4)
struct CompiledShader {
    bool success;
    const uint32_t* spirv_data;
    size_t spirv_size;
    const char* error_message;
};

inline CompiledShader compile_current_shader();
inline bool recreate_compute_pipeline_from_spirv(const uint32_t* spirv, size_t size);
```

## Files to Modify

1. **vulkan/sdf_engine.hpp** - Add new helpers, refactor existing functions
2. **src/vybe/sdf/render.jank** - Add event handling, shader operations
3. **src/vybe/sdf/events.jank** (new) - Event dispatch logic
4. **src/vybe/sdf/shader.jank** (new) - Shader management

## Success Criteria

After migration:
- [x] Event loop runs from jank (poll_events_only + jank dispatch)
- [x] Key handling logic in jank (handle-key-down! in events.jank)
- [x] Scroll handling in jank (handle-scroll! in events.jank)
- [x] Shader switching index management in jank (switch-shader! in render.jank)
- [x] Shader auto-reload in jank (check-shader-reload! in render.jank)
- [x] Shader reload logic in jank (reload-shader! in render.jank, with C++ wrapper for events.jank)
- [ ] Mouse handling logic in jank (currently delegates to C++ for raycast_gizmo)
- [x] All functionality preserved
