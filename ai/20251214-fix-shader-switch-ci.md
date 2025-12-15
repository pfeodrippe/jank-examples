# Fix Shader Switch in CI-built App

## Problem
Shader switching (Page Up/Down or Left/Right keys) worked locally but not in the CI-generated standalone app (DMG/tarball).

## Root Cause
The `load_shader_by_name` function in `vulkan/sdf_engine.hpp` (line 689) was calling `glslangValidator` via `std::system()` to compile GLSL shaders at runtime:

```cpp
std::string compCmd = "glslangValidator -V " + compPath + " -o " + spvPath;
int result = std::system(compCmd.c_str());
```

**Problem**: `glslangValidator` is installed during CI build but NOT bundled into the distributed app. The app bundle includes `libshaderc_shared.dylib` (macOS) / `libshaderc_shared.so` (Linux) but not the CLI tool.

## Solution
Modified `load_shader_by_name` to use the bundled shaderc library directly via the existing `compile_glsl_to_spirv` function instead of calling an external CLI tool:

1. Read GLSL source with `read_text_file()`
2. Compile using `compile_glsl_to_spirv()` (shaderc library - already bundled)
3. Create Vulkan shader module from in-memory SPIR-V

## Changes Made
- `vulkan/sdf_engine.hpp`:
  - Added forward declaration for `read_text_file` (line 620)
  - Added forward declaration for `compile_glsl_to_spirv` (line 644-646)
  - Rewrote `load_shader_by_name` (line 689+) to use shaderc library instead of shell command

## Commands/Files Modified
- `vulkan/sdf_engine.hpp`

## Next Steps
- Test the fix locally with `./bin/run_sdf.sh`
- Push to trigger CI build and verify shader switch works in the generated DMG
