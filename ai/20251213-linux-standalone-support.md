# Linux Standalone Support - Session Summary

## Date: 2025-12-13

## Goal
Make `make sdf-standalone` work on Linux, creating a portable tarball distribution.

## What Was Done

### 1. Linux VM Setup (tart)
- Pulled Ubuntu image: `tart pull ghcr.io/cirruslabs/ubuntu:latest`
- Cloned VM: `tart clone ghcr.io/cirruslabs/ubuntu:latest ubuntu-test`
- Started VM: `tart run ubuntu-test --no-graphics &`
- Installed dependencies: build-essential, cmake, ninja, clang, vulkan, SDL2, patchelf

### 2. Updated `bin/run_sdf.sh` for Linux Support

**Platform detection and environment setup:**
```bash
case "$(uname -s)" in
    Darwin)
        export SDKROOT=...
        export VK_ICD_FILENAMES=...  # MoltenVK
        ;;
    Linux)
        export PATH="$HOME/jank/compiler+runtime/build:$PATH"
        ;;
esac
```

**Platform-specific build arguments:**
```bash
case "$(uname -s)" in
    Darwin)
        JANK_ARGS=(... --framework Cocoa ...)
        DYLIBS=(/opt/homebrew/lib/libvulkan.dylib ...)
        SHARED_LIB_EXT="dylib"
        ;;
    Linux)
        JANK_ARGS=(-I/usr/include/SDL2 ...)
        # Find libs dynamically via ldconfig
        SHARED_LIB_EXT="so"
        ;;
esac
```

**Created `create_linux_standalone()` function:**
- Creates self-contained directory structure (not .app bundle)
- Uses `patchelf` instead of `install_name_tool` for RPATH fixing
- Bundles jank runtime libs (LLVM, clang-cpp, libc++, libunwind)
- Bundles system libs (vulkan, SDL2, shaderc)
- Creates launcher script with `LD_LIBRARY_PATH`
- Creates tarball for distribution

**Key differences from macOS:**
| Feature | macOS | Linux |
|---------|-------|-------|
| Bundle structure | .app | directory |
| Library extension | .dylib | .so |
| Path fixing tool | install_name_tool | patchelf |
| Library path | DYLD_LIBRARY_PATH | LD_LIBRARY_PATH |
| Vulkan | MoltenVK | native |
| Distribution | DMG | tarball |

### 3. Updated CI Workflow

Added to `.github/workflows/ci.yml`:
- Install `patchelf` and `libshaderc-dev` on Linux
- Build standalone app step for Linux
- Upload tarball artifact (SDFViewer-linux.tar.gz)

## Files Modified

- `bin/run_sdf.sh` - Added Linux standalone support
- `.github/workflows/ci.yml` - Added Linux artifact upload

## Commands Used

```bash
# VM management
tart list
tart exec ubuntu-test bash -c "..."

# Install deps in VM
sudo apt-get install -y clang lld patchelf libsdl2-dev libvulkan-dev glslang-tools libshaderc-dev

# Start LLVM build (takes ~2 hours)
cd ~/jank/compiler+runtime && ./bin/build-clang

# Check build progress
tail /tmp/llvm-build.log
```

## What's Next

**Recommended approach: Use CI for Linux testing**

Building jank on Linux locally is complex due to:
- Need LLVM 22 with libc++ (not system libstdc++)
- `std::chrono::parse` requires modern libc++ (not in Ubuntu's libstdc++)
- Complex linker configuration for clang libraries

The CI workflow will handle this properly since it:
- Builds LLVM with the correct configuration
- Caches the LLVM build for subsequent runs
- Has the proper environment for jank compilation

**Alternative if local testing needed:**
1. Use a Docker container with jank pre-built
2. Or wait for jank to provide Linux binary releases

## Local VM Build Issues Encountered

### Issue 1: System LLVM version mismatch
```
CMake Error: Found unsupported version: LLVM 18.1.3
Please set llvm_dir pointing to the llvm version 22.0 to 22.0.x
```
**Fix:** Set `-Dllvm_dir=$HOME/jank/compiler+runtime/build/llvm-install/usr/local`

### Issue 2: Missing submodules
```
CMake Error: The source directory .../third-party/bdwgc does not contain a CMakeLists.txt file
```
**Fix:** `git submodule update --init --recursive`

### Issue 3: std::chrono::parse missing
```
error: no member named 'parse' in namespace 'std::chrono'
```
**Cause:** System libstdc++ doesn't have C++20 `std::chrono::parse`
**Attempted fix:** Build with `-stdlib=libc++`, but causes linker issues

### Issue 4: Linker errors with libc++
```
undefined reference to `clang::NamedDecl::getQualifiedNameAsString() const'
```
**Cause:** Complex ABI mismatch between LLVM built with libstdc++ and jank built with libc++

## Key Learnings

1. **tart exec syntax**: `tart exec <vm-name> bash -c "command"` (no `--`)
2. **jank build-clang needs clang**: The script expects clang to be available to build LLVM
3. **patchelf for RPATH**: On Linux, use `patchelf --set-rpath '$ORIGIN/../lib' binary`
4. **ldconfig -p**: Use to find system library paths dynamically

## Linux Distribution Structure

```
SDFViewer-linux/
├── SDFViewer           # Launcher script
├── bin/
│   ├── SDFViewer-bin   # Actual executable
│   ├── clang-22        # For JIT
│   └── clang++         # Wrapper
├── lib/
│   ├── libLLVM.so
│   ├── libclang-cpp.so
│   ├── libc++.so.1
│   ├── libvulkan.so
│   ├── libSDL2-2.0.so
│   └── clang/22/include/  # Clang headers
├── include/
│   ├── c++/v1/         # C++ stdlib headers
│   └── imgui/          # Project headers
└── resources/
    ├── vulkan_kim/     # Shaders
    └── src/            # jank source files
```

## CI Artifacts

After successful CI run:
- macOS: `SDFViewer.dmg` (Actions > Artifacts > SDFViewer-macOS)
- Linux: `SDFViewer-linux.tar.gz` (Actions > Artifacts > SDFViewer-Linux)
