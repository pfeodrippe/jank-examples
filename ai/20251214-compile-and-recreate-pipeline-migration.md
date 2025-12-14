# Session: Move compile_and_recreate_pipeline to jank

## Date: 2025-12-14

## What Was Done

Moved the orchestration of `compile_and_recreate_pipeline` from C++ to jank by splitting it into two lower-level C++ functions.

### Architecture

**Before:**
```
jank: reload-shader!
  → C++: compile_and_recreate_pipeline(glsl, name)
    → compile GLSL to SPIR-V
    → destroy old pipeline
    → create new pipeline
```

**After:**
```
jank: reload-shader!
  → C++: compile_shader_to_internal_spirv(glsl, name)  -- stores SPIR-V in engine
  → jank: check result
  → C++: recreate_pipeline_from_internal_spirv()       -- uses stored SPIR-V
  → jank: check result, print status
```

### Changes Made

1. **Added to Engine struct** (`vulkan/sdf_engine.hpp:220`):
   ```cpp
   std::vector<uint32_t> pendingSpirvData;  // Temporary SPIR-V storage
   ```

2. **New C++ functions** (`vulkan/sdf_engine.hpp`):
   - `compile_shader_to_internal_spirv(glsl_source, shader_name)` - compiles GLSL, stores SPIR-V internally, returns 0/1
   - `recreate_pipeline_from_internal_spirv()` - uses stored SPIR-V to recreate Vulkan pipeline, returns 0/2

3. **Updated `shader.jank`**:
   - `reload-shader!` now orchestrates the two-step process in jank
   - Calls `compile_shader_to_internal_spirv` first
   - On success, calls `recreate_pipeline_from_internal_spirv`
   - Handles errors at each step

4. **Removed from C++**:
   - `compile_and_recreate_pipeline` convenience wrapper (no longer needed)

### Why Split?

The SPIR-V binary data (`std::vector<uint32_t>`) can't easily pass through jank. By storing it internally in the Engine struct between the two C++ calls, jank can orchestrate the flow while C++ handles the binary data.

### Files Modified

- `vulkan/sdf_engine.hpp` - Added pendingSpirvData field, split function into two, removed old function
- `src/vybe/sdf/shader.jank` - Updated reload-shader! to use split functions

### Testing

```bash
# Test via nREPL
clj-nrepl-eval -p 5557 "(vybe.sdf.shader/reload-shader!)"
# Output: Shader reloaded!
```

### What Stays in C++

- `compile_shader_to_internal_spirv` - Uses shaderc library for GLSL→SPIR-V compilation
- `recreate_pipeline_from_internal_spirv` - All Vulkan API calls (vkDestroyPipeline, vkCreateShaderModule, etc.)
- `read_text_file` - File reading (workaround for jank's buggy slurp)

### What's Now in jank

- File path construction
- Orchestration logic (call compile, check result, call recreate pipeline, check result)
- Error handling and user feedback (println)

## Commands Used

```bash
# Kill existing process and clean
pkill -9 -f jank; rm -rf target/

# Start SDF
./bin/run_sdf.sh > /tmp/sdf_output.txt 2>&1 &

# Wait for nREPL
for i in {1..30}; do nc -z localhost 5557 && break; sleep 1; done

# Test
clj-nrepl-eval -p 5557 "(vybe.sdf.shader/reload-shader!)"
```

## Migration Progress

The C++ to jank migration is now more complete:
- [x] Event loop in jank
- [x] Key handling in jank
- [x] Scroll handling in jank
- [x] Shader index management in jank
- [x] Shader auto-reload in jank
- [x] Shader reload orchestration in jank (NEW)
- [ ] Mouse handling (complex 3D math in raycast_gizmo - intentionally in C++)
