// Fiction Graphics Abstraction Layer
//
// Compile-time dispatch between Vulkan (desktop) and WebGPU (WASM) backends.
// Both backends provide the same API in namespaces `fiction_engine` and `fiction`.
//
// Usage:
//   - Desktop (default): includes Vulkan backend via fiction_gfx_vulkan.hpp
//   - WASM: define FICTION_USE_WEBGPU before including, gets WebGPU backend
//   - JIT/AOT stub: define FICTION_USE_STUB to get minimal declarations only
//
// The jank game code only includes this header and doesn't know which backend
// is active.

#pragma once

#if defined(FICTION_USE_STUB)
  // Minimal stub for JIT/AOT - no real backend dependencies
  #include "fiction_gfx_stub.hpp"
#elif defined(FICTION_USE_WEBGPU)
  #include "fiction_gfx_webgpu.hpp"
#else
  #include "fiction_gfx_vulkan.hpp"
#endif
