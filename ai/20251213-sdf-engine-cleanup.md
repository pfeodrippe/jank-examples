# SDF Engine Cleanup - Removed Unused Code

**Date:** 2025-12-13

## Task
Analyze `sdf_engine.hpp`, identify unused code, and remove it. Test with `make sdf`.

## What I Learned

### Used Functions (37 total from jank)
From `src/vybe/sdf/ui.jank`:
- `imgui_new_frame`, `imgui_render`

From `src/vybe/sdf/render.jank`:
- Core lifecycle: `init`, `cleanup`, `should_close`, `poll_events`, `update_uniforms`, `draw_frame`
- Time: `get_time`
- Camera: `get/set_camera_distance`, `get/set_camera_angle_x`, `get/set_camera_angle_y`, `get/set_camera_target_y`
- Edit mode: `get_edit_mode`, `get_selected_object`, `get_hovered_axis`, `get_dragging_axis`
- Objects: `get_object_pos_x/y/z`, `get_object_rot_x/y/z`, `get_object_type_id`, `get_object_selectable`, `get_object_count`
- Shaders: `get_current_shader_name`, `switch_shader`, `get_pending_shader_switch`, `clear_pending_shader_switch`
- Render mode: `set_continuous_mode`, `set_dirty`
- Screenshot: `save_screenshot`

### Unused Code Removed

1. **Event Recording/Replay System** (~210 lines)
   - `RecordedEvent` struct and global variables
   - Recording functions: `start_recording`, `stop_recording`, `is_recording`, `get_recorded_event_count`
   - Replay functions: `start_replay`, `stop_replay`, `is_replaying`, `process_replay_events`
   - File I/O: `save_recorded_events`, `load_recorded_events`
   - Event injection: `push_event`, `inject_mouse_click`, `inject_mouse_motion`, `inject_key_press`, `inject_scroll`
   - Also removed the recording logic from `poll_events()`

2. **SDFSampler System** (~700+ lines)
   - `SDFSampler` struct and `get_sampler()` accessor
   - Shader extraction: `extract_scene_sdf`, `build_sampler_shader`
   - Sampler lifecycle: `init_sampler`, `cleanup_sampler`
   - Sampling functions: `sample_sdf`, `sample_sdf_point`, `sample_sdf_grid`, `sample_sdf_grid_to_file`
   - `SDFGridStorage` struct and all accessor functions: `create_sdf_grid_storage`, `sample_sdf_grid_to_memory`, `sdf_grid_*` functions

## Commands Executed

```bash
# Analysis
grep sdfx:: *.jank          # Found all used functions
wc -l vulkan/sdf_engine.hpp # Before: 3210 lines

# Edits via Edit tool
# 1. Removed SDFSampler section (lines 2498-3210)
# 2. Removed Event Recording section (lines 2073-2283)
# 3. Removed recording globals (lines 50-60)
# 4. Removed recording logic from poll_events()

# Testing
make sdf                     # Passed!
wc -l vulkan/sdf_engine.hpp # After: 2262 lines
```

## Results

- **Before:** 3210 lines
- **After:** 2262 lines
- **Removed:** 948 lines (~30% reduction)
- **Build:** `make sdf` passes successfully

## What's Next

The file is now cleaner with only actively used code. If needed in the future:
- SDFSampler could be re-added for mesh export functionality
- Event recording could be re-added for automated testing
