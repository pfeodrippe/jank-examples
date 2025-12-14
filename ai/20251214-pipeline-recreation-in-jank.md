# Session: Pipeline Recreation Moved to Pure jank

## Date: 2025-12-14

## Status: COMPLETED

Successfully moved Vulkan pipeline recreation from C++ to jank. The `recreate-pipeline!` function now uses pure jank with minimal C++ helpers only for output parameter handling.

## Architecture

**Before (C++ heavy):**
```
jank: reload-shader!
  -> C++: compile_and_recreate_pipeline()
    -> compile GLSL to SPIR-V
    -> destroy old pipeline
    -> create new pipeline (all Vulkan calls)
```

**After (jank heavy):**
```
jank: reload-shader!
  -> C++: compile_glsl_to_spirv_stored()  -- shaderc is C++ library
  -> jank: recreate-pipeline!
    -> jank: vkDeviceWaitIdle (direct call)
    -> jank: vkDestroyPipeline (direct call)
    -> jank: vkDestroyPipelineLayout (direct call)
    -> jank: vkDestroyShaderModule (direct call)
    -> jank: create shader module (via small helper)
    -> jank: create pipeline layout (via small helper)
    -> jank: create compute pipeline (via small helper)
```

## Key Implementation Details

### Small cpp/raw Helpers (Required for Output Params)

Vulkan create functions use output parameters (`VkPipeline* pPipeline`) which don't work well with pure jank pointer indirection. Added minimal helpers:

```cpp
inline VkShaderModule create_shader_module_jank(VkDevice device, VkShaderModuleCreateInfo* info);
inline VkPipelineLayout create_pipeline_layout_jank(VkDevice device, VkPipelineLayoutCreateInfo* info);
inline VkPipeline create_compute_pipeline_jank(VkDevice device, VkComputePipelineCreateInfo* info);
```

### Pure jank Pattern Using u/merge*

Instead of verbose field-by-field assignment, used the `merge*` macro:

```clojure
(let [module-info (cpp/VkShaderModuleCreateInfo.)]
  (u/merge* module-info
            {:sType vk/VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
             :codeSize spirv-size
             :pCode spirv-data})
  ...)
```

### C++ Accessors Added

- `get_descriptor_set_layout_ptr()` - Returns `const VkDescriptorSetLayout*` for pSetLayouts
- SPIR-V data accessors: `get_pending_spirv_data()`, `get_pending_spirv_size_bytes()`, `clear_pending_spirv()`
- Pipeline handle setters: `set_compute_shader_module()`, `set_compute_pipeline()`, `set_compute_pipeline_layout()`

## Files Modified

- `src/vybe/sdf/shader.jank` - `recreate-pipeline!` function in pure jank
- `vulkan/sdf_engine.hpp` - Added accessors, removed `recreate_pipeline_from_internal_spirv`

## Testing

```bash
make sdf
clj-nrepl-eval -p 5557 "(vybe.sdf.shader/reload-shader!)"
# Output: Shader reloaded!
```

## Lessons Learned

1. **cpp/raw is acceptable for output params** - Vulkan's `vkCreate*` functions use output parameters that require a small wrapper to return values
2. **Use u/merge* for struct init** - Much cleaner than individual field assignments
3. **Value types vs pointers** - Use `cpp/VkType.` for stack values, not `cpp/new VkType`
4. **Null checks** - Use `cpp/!` instead of comparing with nullptr (type mismatch issues)

## What Stays in C++

- GLSL to SPIR-V compilation (shaderc is a C++ library)
- File reading (jank's slurp has bugs with large files)
- Small output parameter wrappers

## What's Now in jank

- All Vulkan destroy calls
- Struct initialization with merge*
- Pipeline creation orchestration
- Error handling and flow control

## Migration Progress Update

- [x] Event loop in jank
- [x] Key handling in jank
- [x] Scroll handling in jank
- [x] Shader index management in jank
- [x] Shader auto-reload in jank
- [x] Shader reload orchestration in jank
- [x] **Pipeline recreation in jank (NEW)**
- [ ] Mouse handling (complex 3D math - intentionally in C++)
