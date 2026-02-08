// Provide stb implementations once for the WebGPU build.
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#ifndef FICTION_USE_WEBGPU
#define FICTION_USE_WEBGPU
#endif
#include "fiction_gfx_webgpu.hpp"
