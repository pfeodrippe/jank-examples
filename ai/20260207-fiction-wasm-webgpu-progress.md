# Fiction WASM WebGPU Implementation Progress - Feb 7, 2026

## Summary

Continued implementation of `make fiction-wasm` to run Fiction narrative game in the browser using WebGPU.

## What We Did

### 1. Fixed WebGPU Headers

Updated `fiction_gfx/fiction_gfx_webgpu.hpp` to use the new emdawnwebgpu port:

- Changed from `#include <webgpu/webgpu.h>` + `<emscripten/html5_webgpu.h>` (old API)
- To `#include <webgpu/webgpu_cpp.h>` (new Dawn-style C++ API)

### 2. Updated to Dawn WebGPU API

The old Emscripten WebGPU API (`-sUSE_WEBGPU=1`) is deprecated. The new emdawnwebgpu port uses:
- Async device/adapter request pattern (not synchronous `emscripten_webgpu_get_device()`)
- C++ wrapper classes (`wgpu::Device`, `wgpu::Queue`, etc.)
- Different type names like `wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector`
- Different status enums: `SuccessOptimal`/`SuccessSuboptimal` instead of `Success`

### 3. Updated Build Scripts

**Makefile** (`fiction_gfx/build-wasm/fiction_gfx_wasm.o` target):
- Added `--use-port=emdawnwebgpu` flag

**bin/run_fiction_wasm.sh**:
- Added `--use-port=emdawnwebgpu` to WebGPU object compilation
- Changed `--em-flag "-sUSE_WEBGPU=1"` to `--em-flag "--use-port=emdawnwebgpu"` for jank

### 4. Fixed API Compatibility Issues

- Changed `SurfaceSourceCanvasHTMLSelector` to `EmscriptenSurfaceSourceCanvasHTMLSelector`
- Changed `SurfaceGetCurrentTextureStatus::Success` to check for `SuccessOptimal` or `SuccessSuboptimal`
- Moved error callback setup from `Device.SetUncapturedErrorCallback()` to `DeviceDescriptor.SetUncapturedErrorCallback()`

## Current Status

- WebGPU object (`fiction_gfx/build-wasm/fiction_gfx_wasm.o`) compiles successfully
- `bin/run_fiction_wasm.sh` starts running but jank crashes with segfault during AOT compilation

## Commands That Work

```bash
# Build WebGPU object only
make fiction_gfx/build-wasm/fiction_gfx_wasm.o

# This works:
em++ -std=c++20 -O2 -c -fPIC \
    --use-port=emdawnwebgpu \
    -DFICTION_USE_WEBGPU -DJANK_TARGET_EMSCRIPTEN -DJANK_TARGET_WASM=1 \
    -DSTBI_NO_THREAD_LOCALS \
    ... includes ...
    fiction_gfx/fiction_gfx_webgpu.cpp \
    -o fiction_gfx/build-wasm/fiction_gfx_wasm.o
```

## Error During Full Build

```
[jank] Saved generated C++ to: .../fiction_generated.cpp
[jank] WASM AOT mode: skipping JIT compilation
[jank-jit] Attempting to compile: #include <fiction_gfx/fiction_gfx.hpp>
[jank-jit] Compilation SUCCEEDED for: #include <fiction_gfx/fiction_gfx.hpp>
...
./bin/emscripten-bundle: line 798: 79996 Segmentation fault: 11
[emscripten-bundle] ERROR: AOT compilation failed!
```

### Analysis of Generated C++

The generated file (`build-wasm/fiction_generated.cpp`) contains:
- clojure.string module code (186 lines, partial)
- Started fiction.parser module but crash happened during generation

The crash happens in jank during AOT code generation, likely during:
1. Compiling fiction.parser module
2. Or one of its transitive dependencies

**This is a jank compiler bug**, not a WebGPU issue.

## Next Steps

1. **Debug jank segfault** - The crash happens after successfully compiling headers. Could be:
   - Memory issue in jank compiler during AOT generation
   - Issue with the generated C++ code
   - Unrelated jank bug

2. **Check generated C++ file**:
   ```bash
   cat /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm/fiction_generated.cpp
   ```

3. **Try simpler test** - Run `make integrated_wasm` to verify the WASM pipeline works for a simpler demo

4. **Implement WebGPU rendering** - Once the build works, `fiction_gfx_webgpu.hpp` has TODO stubs for:
   - Font atlas creation and upload
   - Text vertex buffer management
   - Pipeline creation from embedded WGSL shaders
   - Render pass recording

## Key Files Modified

- `fiction_gfx/fiction_gfx_webgpu.hpp` - Complete rewrite for Dawn/emdawnwebgpu API
- `bin/run_fiction_wasm.sh` - Added `--use-port=emdawnwebgpu` flags
- `Makefile` - Updated `fiction_gfx/build-wasm/fiction_gfx_wasm.o` target

## Reference: New WebGPU API Pattern

```cpp
// Global instance
inline wgpu::Instance& get_instance() {
    static wgpu::Instance instance = wgpuCreateInstance(nullptr);
    return instance;
}

// Async adapter request
get_instance().RequestAdapter(&adapterOpts, wgpu::CallbackMode::AllowSpontaneous,
    [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
        // Then request device...
        adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowSpontaneous,
            [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                // Device ready, configure surface
            });
    });

// Surface creation for Emscripten
wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
canvasDesc.selector = "#canvas";
wgpu::SurfaceDescriptor surfaceDesc{};
surfaceDesc.nextInChain = &canvasDesc;
wgpu::Surface surface = get_instance().CreateSurface(&surfaceDesc);
```
