# Linux CI Fixes

## Date: 2025-12-14/15

## Summary

Fixed multiple issues to get Linux CI builds working for standalone app.

## Issues Fixed

### 1. SDL3 Support for Linux
- **Problem**: Linux was trying to use SDL2, but code uses SDL3
- **Solution**:
  - Updated `vulkan/imgui/Makefile` to detect OS and use pkg-config for SDL3 on Linux
  - Added SDL3 build from source step in CI (not in Ubuntu 24.04 repos)
  - Added X11 dev libs required for SDL3: `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev`

### 2. SDL3 Caching
- **Problem**: SDL3 build takes ~3-4 minutes, rebuilding every time
- **Solution**: Added cache step with `save-always: true` to cache even on failure

### 3. Shaderc Library Name
- **Problem**: macOS uses `libshaderc_shared`, Ubuntu uses `libshaderc`
- **Solution**: Updated `bin/run_sdf.sh` to use correct library name per platform

### 4. stb_impl -fPIC
- **Problem**: `stb_impl.o` wasn't compiled with `-fPIC`, causing linker error for shared library
- **Solution**: Added `-fPIC` flag to stb_impl compilation

### 5. ldconfig for SDL3
- **Problem**: SDL3 installed to `/usr/local/lib` but not in library path
- **Solution**: Added `sudo ldconfig` after SDL3 install

### 6. Swap Space for AOT Compilation
- **Problem**: jank AOT compilation OOM killed during standalone build
- **Solution**: Added 8GB swap space creation to "Build and Test - Linux" job

### 7. Launcher Script Variable Expansion
- **Problem**: Linux launcher used `<< 'LAUNCHER'` (quoted heredoc) which prevented `$APP_NAME` expansion
- **Solution**: Changed to `<< LAUNCHER` (unquoted) and escaped runtime variables with `\$`

## Files Modified

- `.github/workflows/ci.yml` - CI workflow with SDL3 build, caching, swap space
- `bin/run_sdf.sh` - Linux build script fixes (SDL3, shaderc, -fPIC, launcher)
- `vulkan/imgui/Makefile` - Cross-platform SDL3 include paths

## CI Status

- **macOS**: ✅ Passing
- **Linux**: ✅ Passing

Both artifacts are created:
- `SDFViewer-macOS` (DMG)
- `SDFViewer-Linux` (tar.gz)

## Testing Notes

### Lima x86_64 VM Testing (2025-12-15)

Successfully tested Linux binary in Lima x86_64 emulated VM on macOS ARM64:

**Quick Test Script:**
```bash
# One command to setup VM, download artifact, and run:
./bin/test-linux-lima.sh

# Or individual steps:
./bin/test-linux-lima.sh --setup     # Create Lima VM (first time)
./bin/test-linux-lima.sh --download  # Download latest CI artifact
./bin/test-linux-lima.sh --run       # Run in VM
```

**Manual Lima VM Setup:**
```bash
# Install Lima
brew install lima lima-additional-guestagents

# Create x86_64 config
cat > /tmp/lima-x86_64.yaml << 'EOF'
vmType: qemu
arch: x86_64
images:
  - location: "https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-amd64.img"
    arch: "x86_64"
cpus: 2
memory: "4GiB"
EOF

# Start VM
limactl start --name=linux-x64 /tmp/lima-x86_64.yaml

# Run commands
limactl shell linux-x64 uname -a
```

**Issues Found & Fixed:**

### 8. LLVM Versioned Library Symlinks
- **Problem**: Binary looks for `libLLVM.so.22.0git` but we bundle `libLLVM.so`
- **Solution**: Create versioned symlinks: `ln -sf libLLVM.so libLLVM.so.22.0git`

### 9. Architecture-Specific Include Path
- **Problem**: `__config_site` header is in `include/x86_64-unknown-linux-gnu/c++/v1/`
- **Solution**: Added architecture-specific path to CPATH in launcher and clang++ wrapper

### 10. libc++ vs libstdc++ Header Conflict
- **Problem**: JIT picks up both bundled libc++ AND system libstdc++ headers
- **Solution**: clang++ wrapper now uses `-nostdinc++` to exclude system C++ headers

### 11. SDL3/Vulkan Library Bundling
- **Problem**: SYSTEM_LIBS created symlinks pointing to non-existent files
- **Solution**: Copy actual library files and create proper symlinks

**Testing Results:**
- ✅ Binary loads correctly (all shared libraries resolve)
- ✅ Launcher script and clang++ wrapper work correctly
- ✅ SDL3 and Vulkan properly bundled
- ❌ JIT compilation fails due to libc++/libstdc++ header conflict

**Root Cause (jank issue):**
jank's internal JIT uses libclang APIs directly (not the CXX env var wrapper).
libclang has hardcoded include paths that include system libstdc++ headers,
which conflict with bundled libc++ headers.

**Required jank fix:**
Pass `-nostdinc++` to jank's internal JIT compilation to exclude system C++ headers.
This needs to be fixed in jank's `compiler+runtime` code.

## Tart VM Usage

- Cannot run x86_64 Linux binary on ARM64 tart VM (architecture mismatch)
- Use `tart exec` to run commands in VM without SSH:
```bash
tart exec ubuntu-sdf-test bash -c "command here"
```

See memory file `tart-vm-linux-testing` for full details.
