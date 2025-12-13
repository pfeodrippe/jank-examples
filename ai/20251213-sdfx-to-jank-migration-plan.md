# Migration Plan: sdf_engine.hpp to jank

**Goal:** Move as much logic as possible from C++ (`vulkan/sdf_engine.hpp`) to jank, making code reloadable and simplifying the architecture.

## Current Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ vulkan/sdf_engine.hpp (~3500 lines)                         │
│ ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│ │ STATE           │  │ VULKAN          │  │ SDL          │ │
│ │ - Engine struct │  │ - init()        │  │ - poll_events│ │
│ │ - Camera        │  │ - draw_frame()  │  │ - handle_*   │ │
│ │ - Objects[]     │  │ - cleanup()     │  │              │ │
│ │ - EditMode      │  │ - shaders       │  │              │ │
│ │ - Flags         │  │                 │  │              │ │
│ └─────────────────┘  └─────────────────┘  └──────────────┘ │
│ ┌─────────────────┐  ┌─────────────────┐                    │
│ │ IMGUI WRAPPERS  │  │ GETTERS/SETTERS │                    │
│ │ - imgui_begin   │  │ - get/set_*     │                    │
│ │ - imgui_button  │  │ - ~40 functions │                    │
│ │ - imgui_text    │  │                 │                    │
│ └─────────────────┘  └─────────────────┘                    │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│ src/vybe/sdf.jank (thin wrappers)                           │
│ - Calls cpp/sdfx.* functions                                │
│ - Duplicated camera-states atom (not synced with C++)       │
└─────────────────────────────────────────────────────────────┘
```

## Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ vulkan/sdf_bindings.hpp (~800 lines) - STATELESS            │
│ ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│ │ VULKAN BINDINGS │  │ SDL BINDINGS    │  │ IMGUI        │ │
│ │ - vk_init()     │  │ - sdl_poll()    │  │ (via header  │ │
│ │ - vk_render()   │  │ - sdl_time()    │  │  require)    │ │
│ │ - vk_cleanup()  │  │                 │  │              │ │
│ │ - vk_update_ubo │  │                 │  │              │ │
│ │   (params)      │  │                 │  │              │ │
│ └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│ src/vybe/sdf.jank - ALL STATE + LOGIC                       │
│ ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│ │ STATE (atoms)   │  │ INPUT HANDLING  │  │ UI           │ │
│ │ - *camera*      │  │ - handle-mouse! │  │ - draw-ui!   │ │
│ │ - *objects*     │  │ - handle-key!   │  │ - (imgui/*)  │ │
│ │ - *edit-mode*   │  │ - handle-scroll!│  │              │ │
│ │ - *dirty*       │  │                 │  │              │ │
│ └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Benefits

1. **Hot-reloadable state management** - Camera, objects, UI logic all reloadable via nREPL
2. **No JIT/AOT state sharing issues** - C++ is stateless, no ODR problems
3. **Standalone app works** - All logic pre-compiled in jank, C++ just thin bindings
4. **Smaller C++ surface** - ~800 lines vs ~3500 lines
5. **Better debugging** - State visible in jank REPL

## Migration Phases

### Phase 1: Create New Namespace Structure

Create `src/vybe/sdf/` directory structure:
- `state.jank` - All atoms (camera, objects, edit mode, etc.)
- `input.jank` - Input handling (mouse, keyboard, scroll)
- `render.jank` - Thin wrappers for Vulkan calls
- `ui.jank` - ImGui via header require

### Phase 2: Move Camera State (LOW RISK)

**Already partially done!** The `camera-states` atom exists.

**What to move:**
- Camera struct fields: `distance`, `angleX`, `angleY`, `targetX/Y/Z`
- `get/set_camera_*` functions → jank atom operations

**C++ change:**
```cpp
// BEFORE: State in Engine struct
struct Engine {
    Camera camera;
};

// AFTER: No camera in Engine, pass as parameters
inline void update_uniforms(double dt,
                            float cam_dist, float cam_ax, float cam_ay,
                            float cam_tx, float cam_ty, float cam_tz) {
    //g Use params directly
}
```

**jank change:**
```clojure
;; src/vybe/sdf/state.jank
(ns vybe.sdf.state)

(defonce *camera*
  (atom {:distance 8.0
         :angle-x 0.3
         :angle-y 0.0
         :target-x 0.0
         :target-y 0.0
         :target-z 0.0}))

;; Mutators (called from input handlers)
(defn update-camera! [f & args]
  (apply swap! *camera* f args))
```

### Phase 3: Move Edit Mode State (LOW RISK)

**What to move:**
- `editMode`, `selectedObject`, `hoveredAxis`, `draggingAxis`
- Various request flags: `undoRequested`, `redoRequested`, etc.

**jank:**
```clojure
;; src/vybe/sdf/state.jank
(defonce *edit-mode*
  (atom {:enabled false
         :selected-object -1
         :hovered-axis -1
         :dragging-axis -1
         :undo-requested false
         :redo-requested false
         :duplicate-requested false
         :delete-requested false}))
```

### Phase 4: Move Objects to jank (MEDIUM RISK)

**What to move:**
- `std::vector<SceneObject> objects` → jank vector of maps
- Object manipulation: add, remove, update position/rotation

**jank:**
```clojure
;; src/vybe/sdf/state.jank
(defonce *objects*
  (atom [{:position [0 0 0] :rotation [0 0 0] :type 0 :selectable true}]))

(defn add-object! [obj]
  (swap! *objects* conj obj))

(defn update-object! [idx f & args]
  (swap! *objects* update idx #(apply f % args)))
```

**C++ change:**
- `update_uniforms()` receives object data as parameters
- Create helper to convert jank data → UBO format

### Phase 5: Use Header Require for ImGui (LOW RISK)

**Remove C++ wrappers:**
```cpp
// DELETE these from sdf_engine.hpp:
inline void imgui_begin(const char* name) { ImGui::Begin(name); }
inline void imgui_end() { ImGui::End(); }
inline void imgui_text(const char* text) { ImGui::Text("%s", text); }
// ... ~15 more wrapper functions
```

**jank:**
```clojure
(ns vybe.sdf.ui
  (:require
   ;; Direct ImGui access via header require
   ["imgui.h" :as imgui :scope "ImGui"]
   ["imgui.h" :as imgui-h :scope ""]))  ;; For constants

(defn draw-editor-ui! [state]
  (imgui/Begin "Editor")
  (imgui/Text #cpp "Selected: %d" (:selected-object @state))
  (when (imgui/Button "Reset Camera")
    (reset-camera!))
  (imgui/End))
```

### Phase 6: Move Input Handling to jank (MEDIUM RISK)

**What to move:**
- `handle_mouse_button()`, `handle_mouse_motion()`, `handle_scroll()`
- `handle_key_down()`
- Gizmo raycast logic (complex but doable)

**C++ keeps:**
- `poll_events()` but returns raw SDL events or calls jank callbacks

**Option A: Poll returns event data**
```cpp
// Returns event type + data for jank to process
struct EventData {
    int type;
    float x, y, dx, dy;
    int key;
    int button;
    bool pressed;
};
inline EventData poll_next_event();
```

**Option B: C++ calls jank callbacks (complex)**
- Requires jank callable from C++, not recommended initially

**Recommendation:** Start with Option A - simpler, jank processes events

### Phase 7: Simplify Recording/Replay (OPTIONAL)

The event recording system is ~300 lines. Could be:
- Left in C++ (low priority to migrate)
- Or migrated if we have event data in jank

## Implementation Order

| Phase | Risk | Lines Removed | Priority |
|-------|------|---------------|----------|
| 1. Namespace structure | None | 0 | High |
| 2. Camera state | Low | ~50 | High |
| 3. Edit mode state | Low | ~100 | High |
| 5. ImGui header require | Low | ~60 | High |
| 4. Objects | Medium | ~150 | Medium |
| 6. Input handling | Medium | ~200 | Medium |
| 7. Recording | Low | ~300 | Low |

## New File Structure

```
src/vybe/sdf/
├── state.jank      # All atoms: camera, objects, edit-mode, etc.
├── input.jank      # Input handling in jank
├── render.jank     # Thin wrappers for Vulkan (init, draw, cleanup)
├── ui.jank         # ImGui via header require
└── shader.jank     # Shader switching logic

src/vybe/sdf.jank   # Main entry point, run! function
```

## C++ Bindings After Migration

```cpp
// vulkan/sdf_bindings.hpp - STATELESS, ~800 lines

namespace sdfx {

// ============ Core Vulkan lifecycle ============
bool vk_init(const char* shader_dir);
void vk_cleanup();
bool vk_should_close();  // SDL window closed?
double vk_get_time();

// ============ Rendering ============
// UBO data passed from jank - no internal state
void vk_update_ubo(
    // Camera
    float cam_dist, float cam_ax, float cam_ay,
    float cam_tx, float cam_ty, float cam_tz,
    // Edit mode
    bool edit_enabled, int selected_obj, int hovered_axis,
    // Object count (objects passed separately)
    int object_count
);

// Object data as flat arrays (jank provides)
void vk_set_object_positions(float* positions, int count);
void vk_set_object_rotations(float* rotations, int count);

void vk_draw_frame();

// ============ Events ============
// Returns raw event data for jank to process
struct SDLEvent {
    int type;  // 0=none, 1=mouse_button, 2=mouse_motion, 3=scroll, 4=key
    float x, y, dx, dy;
    int key;
    int button;
    bool pressed;
};
SDLEvent vk_poll_event();

// ============ Shader management ============
void vk_scan_shaders();
int vk_get_shader_count();
const char* vk_get_shader_name(int idx);
void vk_load_shader(int idx);
void vk_reload_current_shader();

// ============ Screenshot ============
bool vk_save_screenshot(const char* path);

}
```

## Migration Checklist

### Phase 1: Setup ✅ COMPLETED (2025-12-13)
- [x] Create `src/vybe/sdf/state.jank`
- [x] Create `src/vybe/sdf/render.jank`
- [x] Update `src/vybe/sdf/ui.jank` (minimal wrapper)
- NOTE: `input.jank` not yet created - input handling still in C++

### Phase 2: Camera ✅ COMPLETED (2025-12-13)
- [x] Add camera atom to `state.jank` (*camera*, *camera-presets*)
- [x] Add sync-camera-to-cpp! and sync-camera-from-cpp! functions
- [x] Camera state managed by jank, synced to C++ via existing set_camera_* functions
- NOTE: C++ still has camera getters/setters - called by jank wrappers

### Phase 3: Edit Mode ✅ COMPLETED (2025-12-13)
- [x] Add `*edit-mode*` atom to `state.jank`
- [x] Add `sync-edit-mode-from-cpp!` to read state from C++
- [x] Edit mode state synced each frame in main loop
- NOTE: C++ still owns the state, jank reads via sync function

### Phase 4: Objects ✅ COMPLETED (2025-12-13)
- [x] Add `*objects*` atom to `state.jank`
- [x] Add individual accessors to C++ (get_object_pos_x/y/z, get_object_rot_x/y/z, etc.)
- [x] Add `sync-objects-from-cpp!` to read objects at startup
- [x] "Objects loaded: 6" confirmed in output
- NOTE: C++ still owns objects, jank reads via sync. Next step: jank writes to C++

### Phase 5: ImGui ✅ COMPLETED (2025-12-13)
- [x] Add header require for imgui.h in ui.jank
- [x] Remove imgui wrapper functions from C++ (done in Session 3)
- [x] Rewrite UI using direct imgui calls (draw-debug-ui!)
- [x] Header require pattern: `["imgui.h" :as imgui :scope "ImGui"]`
- [x] Debug panel shows: FPS, Camera state, Edit mode, Objects count
- NOTE: imgui_new_frame/imgui_render still in C++ (backend init needs Vulkan/SDL context)

### Phase 6: Input - DEFERRED
- [ ] Create `SDLEvent` struct and `vk_poll_event()`
- [ ] Implement `handle-event!` in jank
- [ ] Remove `handle_*` functions from C++
- STATUS: Deferred - complex, deeply integrated with C++ gizmo logic

## Testing Strategy

1. **Incremental migration** - One phase at a time, test after each
2. **Both modes** - Test `make sdf` (JIT) and `make sdf-standalone` (AOT)
3. **nREPL testing** - Verify hot-reloading works for migrated code
4. **Screenshot regression** - Compare screenshots before/after migration

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Performance degradation | Profile after each phase, jank→C++ boundary is fast |
| ImGui header conflicts | Use `:scope "ImGui"` carefully, test thoroughly |
| Float/double conversion | Use `(cpp/float. x)` consistently |
| Object data marshaling | Benchmark flat array approach vs per-object calls |

## Questions to Resolve

1. **Input handling approach**: Poll returning events vs callbacks?
2. **Object data format**: Flat arrays or structured data from jank?
3. **Shader list**: Keep in C++ or move to jank atom?
4. **Recording system**: Migrate or keep in C++ (low priority)?

## Success Criteria

- [ ] `make sdf` works with hot-reloading of all jank code
- [ ] `make sdf-standalone` works without needing header bundling
- [ ] Direct `cpp/sdfx.*` calls from nREPL not needed (use jank functions)
- [ ] C++ code reduced by ~75% (3500 → 800 lines)
- [ ] No state duplication between jank and C++
