# Standalone Build Implementation

## Summary
Successfully implemented `--standalone` flag in `bin/run_sdf.sh` to generate standalone executables from jank code.

## What Was Done

### 1. Added CLI flags to `bin/run_sdf.sh`
- `--standalone` - Build a standalone executable instead of running via JIT
- `-o|--output <name>` - Specify output executable name (default: `sdf-viewer`)

### 2. Key Discovery: jank `--obj` Bug
Discovered a bug in jank's AOT processor (`/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/aot/processor.cpp`):
- Object files from `--obj` are added at lines 1145-1149
- This is AFTER the `-x c++` flag at lines 1035-1037
- Clang interprets files after `-x c++` as C++ source, not object files
- **Result**: Object files get treated as source files, causing build failures

### 3. Workaround Implemented
Instead of using `--obj` for individual object files:
1. Create a shared library (`.dylib`) from all object files
2. Use `--jit-lib` for JIT symbol resolution
3. Use `--link-lib` for AOT linker

```bash
# Create shared library from object files
clang++ -dynamiclib -o "$SHARED_LIB" "${OBJ_FILES[@]}" \
    -framework Cocoa ... -Wl,-undefined,dynamic_lookup

# Pass to jank
jank --jit-lib "$SHARED_LIB" --link-lib "$SHARED_LIB" compile -o "$OUTPUT_NAME" vybe.sdf
```

### 4. Other Fixes Applied
- Created `vulkan/stb_impl.c` for STB_IMAGE_WRITE_IMPLEMENTATION (avoids duplicate symbols)
- Removed duplicate `#define STB_IMAGE_WRITE_IMPLEMENTATION` from `vulkan/sdf_engine.hpp`
- Added `:refer-clojure :exclude [run!]` to `vybe.sdf` namespace
- Added `:refer-clojure :exclude [abs]` to `vybe.sdf.math` namespace

## Usage

```bash
# JIT mode (default)
./bin/run_sdf.sh

# Standalone build
./bin/run_sdf.sh --standalone

# Custom output name
./bin/run_sdf.sh --standalone -o my-viewer
```

## Result
- Generated `sdf-viewer` executable: 118MB arm64 Mach-O binary
- Successfully runs independently
- All Vulkan/ImGui/SDL3 functionality works

## jank CLI Options Learned
- `--jit-lib <path>` - Library loaded during JIT compilation
- `--link-lib <path>` - Library passed to AOT linker only
- `--obj <path>` - Object file (BUGGY: treated as source after -x c++)
- `-l <path>` - Dynamic library (JIT mode only)
- `compile -o <name> <ns>` - Compile namespace to standalone executable

## Next Steps
- Consider reporting the `--obj` bug to jank maintainers
- The fix would be moving lines 1145-1149 to before line 1035 (before `-x c++`)
