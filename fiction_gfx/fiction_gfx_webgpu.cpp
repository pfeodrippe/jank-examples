// Provide stb implementations once for the WebGPU build.
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#ifndef FICTION_USE_WEBGPU
#define FICTION_USE_WEBGPU
#endif
#include "fiction_gfx_webgpu.hpp"

// =============================================================================
// Global variable definitions for ODR-safe linkage
// =============================================================================
// All inline functions in the header return references to these globals,
// ensuring that all translation units share the same instances.

namespace fiction {
    // TextRenderer and related globals
    TextRenderer* g_text_renderer = nullptr;
    std::vector<DialogueEntry> g_pending_history;
    std::vector<DialogueEntry> g_pending_choices;
    
    // BackgroundRenderer global
    BackgroundRenderer* g_bg_renderer = nullptr;
    
    // ParticleRenderer global
    ParticleRenderer* g_particle_renderer = nullptr;
    
    // Font path global
    std::string g_font_path = "CrimsonPro-Regular.ttf";
}

namespace fiction_engine {
    // WebGPU instance - must be created before use
    wgpu::Instance g_instance = wgpuCreateInstance(nullptr);
    
    // Engine pointer
    Engine* g_engine_ptr = nullptr;
    
    // Render callback
    RenderCallback g_render_callback = nullptr;
    
    // Event queue for input handling
    std::vector<InputEvent> g_event_queue;
}
