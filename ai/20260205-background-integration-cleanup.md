# 2026-02-05: Background Image Integration + C++ Cleanup

## What was done

### Principle: Minimize C++, maximize jank
Applied the rule that C++ should only contain essential Vulkan plumbing. Everything else should be in jank.

### 1. Simplified C++ Background API
- **Before**: `load_background_image()` took 8 Vulkan params (VkDevice, VkPhysicalDevice, VkQueue, VkCommandPool, VkRenderPass, screenWidth, screenHeight, shaderDir)
- **After**: Added `load_background_image_simple(filepath, shaderDir)` that fetches Vulkan handles internally from `fiction_engine` and gets screen dimensions from the text renderer
- Location: `vulkan/fiction_text.hpp:647-661`

### 2. Removed FileWatcher from C++ (~55 lines deleted)
- Removed `FileWatcher` struct, `init_file_watcher()`, `check_file_modified()`, `cleanup_file_watcher()` from `fiction_text.hpp`
- Removed `#include <sys/stat.h>` from `fiction_text.hpp`

### 3. Implemented file watching in jank
- Added `get_file_mod_time(path)` to `fiction_engine.hpp` (5 lines of C++ - wraps `stat()` which jank can't do natively)
- Added `#include <sys/stat.h>` to `fiction_engine.hpp`
- In `fiction.jank`: added `*last-mod-time` atom and `file-modified?` function using `engine/get_file_mod_time`
- Main loop now checks `(file-modified?)` each frame and reloads story if changed

### 4. Wired background rendering
- Created `record_all_commands()` composite callback: calls `record_bg_commands()` then `record_text_commands()`
- `init_text_renderer` now registers `record_all_commands` instead of just `record_text_commands`
- `cleanup_text_renderer()` now also calls `cleanup_bg_renderer()`
- Added `init-background!` function in `fiction/render.jank`
- `fiction.jank` init! now calls `(text-render/init-background! bg-image-path "vulkan_fiction")`

### 5. Panel positioning
- Added `set_panel_position(x, width)` C++ setter in `fiction_text.hpp`
- Exposed as `set-panel-position!` in `fiction/render.jank`
- Panel positioned at X: 56%, width: 40% to overlay the black right bar of bg-1.png
- Panel background set to semi-transparent black (0, 0, 0, 0.6) to let bg image show through

### 6. Makefile updated
- Added `bg.vert` and `bg.frag` to `FICTION_SHADERS_SRC`

## Files modified
- `vulkan/fiction_text.hpp` - simplified bg API, removed FileWatcher, added composite callback, set_panel_position
- `vulkan/fiction_engine.hpp` - added `get_file_mod_time()`, `#include <sys/stat.h>`
- `src/fiction.jank` - bg init, file watching, panel positioning
- `src/fiction/render.jank` - `init-background!`, `set-panel-position!`
- `Makefile` - bg shaders in FICTION_SHADERS_SRC

## Commands used
- Read/edit operations on source files
- No build/run attempted this session

## What to do next
1. **Build and test** - Run `make fiction` to see if everything compiles and the background renders
2. **Tune panel position** - The 56%/40% positioning is a guess based on image description; may need adjustment after seeing it render
3. **Hot-reload testing** - Edit `stories/la_voiture.md` while running and verify it reloads without losing state
4. **Flecs ECS migration** - `fiction/state.jank` still uses atoms heavily; user wants `vf/defsystem` for state management
5. **Consider**: The panel bg alpha (0.6) may need tuning - if the bg image's black brushstroke is dark enough, could go lower or even 0.0
