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

- Tested Linux binary download in tart VM using `tart exec`
- Cannot run x86_64 Linux binary on ARM64 VM (architecture mismatch)
- Would need x86_64 Linux machine to fully test the standalone

## Tart VM Usage

Key discovery: Use `tart exec` to run commands in VM without SSH:
```bash
tart exec ubuntu-sdf-test bash -c "command here"
```

See memory file `tart-vm-linux-testing` for full details.
