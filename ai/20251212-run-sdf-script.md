# SDF Run Script Creation

## What I learned
- `vybe.sdf` was derived from `sdf.kim` in `~/dev/sdf-3d`
- It only needs `new-frame!` and `render!` from sdf.ui (no sculpt dependency)
- Requires vulkan/sdf_engine.hpp and imgui object files
- Needs vulkan_kim shader directory with .spv files

## Files copied from sdf-3d
- `src/sdf/math.jank` - math utilities (no dependencies)
- `vulkan/sdf_engine.hpp` - main Vulkan SDF engine
- `vulkan/stb_image_write.h` - screenshot support
- `vulkan/Makefile` - build configuration
- `vulkan_kim/*.comp` - compute shader sources (compiled at runtime via shaderc)
- `vulkan_kim/blit.vert`, `blit.frag` - blit shader sources (compiled by run script)

## Files created
- `src/sdf/ui.jank` - minimal UI with just `new-frame!` and `render!` (no sculpt!)
- `bin/run_sdf.sh` - self-contained run script

## Commands executed
```bash
mkdir -p src/sdf vulkan/imgui
cp ~/dev/sdf-3d/src/sdf/math.jank src/sdf/
cp ~/dev/sdf-3d/vulkan/{sdf_engine.hpp,stb_image_write.h,Makefile} vulkan/
cp ~/dev/sdf-3d/vulkan/imgui/*.o vulkan/imgui/
cp -r ~/dev/sdf-3d/vulkan_kim .
chmod +x bin/run_sdf.sh
```

## Usage
```bash
./bin/run_sdf.sh
```

## Notes
- sculpt.jank NOT copied (per user request)
- sdf.ui created minimal (only new-frame!, render!) to avoid sculpt dependency
- Script is self-contained, no references to sdf-3d
- `.spv` and `.o` files are gitignored (compiled on demand)
- `*.comp` shaders compiled at runtime by sdf_engine via shaderc
- `blit.vert/frag` compiled to `.spv` by run script using glslangValidator
- imgui .o files need to be rebuilt locally (see vulkan/imgui/Makefile)
- **C++ namespace changed from `sdf` to `sdfx`** in sdf_engine.hpp to avoid collision with `vybe::sdf` jank namespace
- All `cpp/sdf.X` calls changed to `cpp/sdfx.X` in vybe/sdf.jank and sdf/ui.jank
- Segfault on cleanup is a known issue (viewer works fine otherwise)
