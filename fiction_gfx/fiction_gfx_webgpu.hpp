// Fiction Graphics — WebGPU Backend (WASM)
//
// Full WebGPU implementation for running Fiction in the browser.
// Uses Emscripten's emdawnwebgpu port (Dawn-based WebGPU).

#pragma once

#ifdef __EMSCRIPTEN__

#include <webgpu/webgpu_cpp.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <functional>

// stb_truetype for font rendering (header only, no implementation here)
#include "stb_truetype.h"

// stb_image for background loading
#ifndef STBI_NO_THREAD_LOCALS
#define STBI_NO_THREAD_LOCALS
#endif
#include "stb_image.h"

// =============================================================================
// Forward declarations
// =============================================================================

namespace fiction_engine {
    struct Engine;
    inline Engine*& get_engine();
}

namespace fiction {

// =============================================================================
// Shared data structures (must match Vulkan backend exactly)
// =============================================================================

struct GlyphInfo {
    float u0, v0, u1, v1;
    float width, height, advance, xoffset, yoffset;
};

struct FontAtlas {
    wgpu::Texture texture = nullptr;
    wgpu::TextureView textureView = nullptr;
    wgpu::Sampler sampler = nullptr;
    
    uint32_t atlasWidth = 512;
    uint32_t atlasHeight = 512;
    
    std::unordered_map<int32_t, GlyphInfo> glyphs;
    float lineHeight = 24.0f;
    float spaceWidth = 8.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float scale = 0.0f;
    
    stbtt_fontinfo fontInfo;
    std::vector<uint8_t> fontData;
};

struct TextVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

enum class EntryType {
    Dialogue,
    Narration,
    Choice,
    ChoiceSelected
};

struct DialogueEntry {
    EntryType type;
    std::string speaker;
    std::string text;
    float speakerR, speakerG, speakerB;
    bool selected = false;
};

struct PanelStyle {
    float panelX = 0.70f;
    float panelWidth = 0.30f;
    float panelPadding = 20.0f;
    float bottomMargin = 250.0f;
    float lineSpacing = 4.0f;
    float entrySpacing = 2.0f;
    float choiceIndent = 20.0f;
    float textScale = 1.0f;
    float speakerScale = 1.25f;
    float bgR = 0.0f, bgG = 0.0f, bgB = 0.0f, bgA = 0.0f;
    float textR = 0.8f, textG = 0.8f, textB = 0.8f;
    float narrationR = 0.75f, narrationG = 0.75f, narrationB = 0.78f;
    float choiceR = 0.85f, choiceG = 0.55f, choiceB = 0.25f;
    float choiceHoverR = 1.0f, choiceHoverG = 0.8f, choiceHoverB = 0.4f;
    float choiceSelectedR = 0.5f, choiceSelectedG = 0.5f, choiceSelectedB = 0.5f;
    float choiceSelectedHoverR = 0.7f, choiceSelectedHoverG = 0.7f, choiceSelectedHoverB = 0.7f;
};

struct ChoiceBounds {
    float y0, y1;
    int index;
};

struct TextRenderer {
    wgpu::Device device = nullptr;
    wgpu::Queue queue = nullptr;
    
    FontAtlas font;
    
    wgpu::RenderPipeline textPipeline = nullptr;
    wgpu::BindGroup textBindGroup = nullptr;
    wgpu::BindGroupLayout textBindGroupLayout = nullptr;
    wgpu::Buffer uniformBuffer = nullptr;
    wgpu::Buffer vertexBuffer = nullptr;
    
    uint32_t maxVertices = 65536;
    uint32_t vertexCount = 0;
    std::vector<TextVertex> vertices;
    
    float screenWidth = 1280.0f;
    float screenHeight = 720.0f;
    
    PanelStyle style;
    
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float targetScrollOffset = 0.0f;
    float scrollAnimationSpeed = 16.0f;
    bool autoScrollEnabled = true;
    bool isAutoScrolling = false;
    int lastEntryCount = 0;
    
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    int hoveredChoice = -1;
    
    std::vector<ChoiceBounds> choiceBounds;
    
    bool initialized = false;
};

// Global accessors
inline TextRenderer*& get_text_renderer() {
    static TextRenderer* ptr = nullptr;
    return ptr;
}

inline std::vector<DialogueEntry>& get_pending_history() {
    static std::vector<DialogueEntry> entries;
    return entries;
}

inline std::vector<DialogueEntry>& get_pending_choices() {
    static std::vector<DialogueEntry> entries;
    return entries;
}

// =============================================================================
// Background Renderer
// =============================================================================

struct BackgroundRenderer {
    wgpu::Texture texture = nullptr;
    wgpu::TextureView textureView = nullptr;
    wgpu::Sampler sampler = nullptr;
    
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::Buffer uniformBuffer = nullptr;
    wgpu::Buffer vertexBuffer = nullptr;
    
    int imgWidth = 0;
    int imgHeight = 0;
    
    bool initialized = false;
};

inline BackgroundRenderer*& get_bg_renderer() {
    static BackgroundRenderer* ptr = nullptr;
    return ptr;
}

// =============================================================================
// Particle Renderer (for speaker name effects)
// =============================================================================

struct ParticleRenderer {
    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::Buffer uniformBuffer = nullptr;
    wgpu::Buffer vertexBuffer = nullptr;
    
    uint32_t maxVertices = 4096;
    uint32_t vertexCount = 0;
    std::vector<TextVertex> vertices;
    
    float startTime = 0.0f;
    
    bool initialized = false;
};

inline ParticleRenderer*& get_particle_renderer() {
    static ParticleRenderer* ptr = nullptr;
    return ptr;
}

// =============================================================================
// Font path
// =============================================================================

inline std::string& get_font_path() {
    static std::string path = "fonts/NotoSans-Regular.ttf";
    return path;
}

inline void set_font_path(const std::string& path) {
    get_font_path() = path;
}

// =============================================================================
// WGSL Shader Sources (embedded as strings)
// =============================================================================

inline const char* TEXT_SHADER_WGSL = R"(
struct Uniforms {
    screenSize: vec2<f32>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontAtlas: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) color: vec4<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    var ndc: vec2<f32>;
    ndc.x = (in.position.x / uniforms.screenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (in.position.y / uniforms.screenSize.y) * 2.0;
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.texCoord = in.texCoord;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let coverage = textureSample(fontAtlas, fontSampler, in.texCoord).r;
    let outColor = vec4<f32>(in.color.rgb, in.color.a * coverage);
    if (outColor.a < 0.01) {
        discard;
    }
    return outColor;
}
)";

inline const char* BG_SHADER_WGSL = R"(
struct Uniforms {
    screenSize: vec2<f32>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var bgTexture: texture_2d<f32>;
@group(0) @binding(2) var bgSampler: sampler;

struct VertexInput {
    @location(0) position: vec2<f32>,
    @location(1) texCoord: vec2<f32>,
    @location(2) color: vec4<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    var ndc: vec2<f32>;
    ndc.x = (in.position.x / uniforms.screenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (in.position.y / uniforms.screenSize.y) * 2.0;
    out.position = vec4<f32>(ndc, 0.0, 1.0);
    out.texCoord = in.texCoord;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let texColor = textureSample(bgTexture, bgSampler, in.texCoord);
    return texColor * in.color;
}
)";

// =============================================================================
// Stub implementations - TODO: Implement WebGPU rendering
// =============================================================================

inline bool text_renderer_initialized() {
    auto* tr = get_text_renderer();
    return tr && tr->initialized;
}

inline void scroll_dialogue(float delta) {
    auto* tr = get_text_renderer();
    if (!tr) return;
    tr->scrollOffset = std::max(0.0f, std::min(tr->scrollOffset - delta * 30.0f, tr->maxScroll));
}

inline void scroll_to_bottom(bool animated = true) {
    auto* tr = get_text_renderer();
    if (!tr) return;
    if (animated) {
        tr->targetScrollOffset = tr->maxScroll;
        tr->isAutoScrolling = true;
    } else {
        tr->scrollOffset = tr->maxScroll;
    }
}

inline void set_panel_colors(float r, float g, float b, float a) {
    auto* tr = get_text_renderer();
    if (!tr) return;
    tr->style.bgR = r; tr->style.bgG = g; tr->style.bgB = b; tr->style.bgA = a;
}

inline void set_panel_position(float x, float width) {
    auto* tr = get_text_renderer();
    if (!tr) return;
    tr->style.panelX = x; tr->style.panelWidth = width;
}

inline void set_text_scale(float scale) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.textScale = scale;
}

inline void set_speaker_scale(float scale) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.speakerScale = scale;
}

inline void set_line_spacing(float spacing) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.lineSpacing = spacing;
}

inline void set_entry_spacing(float spacing) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.entrySpacing = spacing;
}

inline void set_panel_padding(float padding) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.panelPadding = padding;
}

inline void set_choice_indent(float indent) {
    auto* tr = get_text_renderer();
    if (tr) tr->style.choiceIndent = indent;
}

inline void set_text_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.textR = r; tr->style.textG = g; tr->style.textB = b; }
}

inline void set_narration_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.narrationR = r; tr->style.narrationG = g; tr->style.narrationB = b; }
}

inline void set_choice_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.choiceR = r; tr->style.choiceG = g; tr->style.choiceB = b; }
}

inline void set_choice_hover_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.choiceHoverR = r; tr->style.choiceHoverG = g; tr->style.choiceHoverB = b; }
}

inline void set_choice_selected_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.choiceSelectedR = r; tr->style.choiceSelectedG = g; tr->style.choiceSelectedB = b; }
}

inline void set_choice_selected_hover_color(float r, float g, float b) {
    auto* tr = get_text_renderer();
    if (tr) { tr->style.choiceSelectedHoverR = r; tr->style.choiceSelectedHoverG = g; tr->style.choiceSelectedHoverB = b; }
}

inline uint32_t get_text_vertex_count() {
    auto* tr = get_text_renderer();
    return tr ? tr->vertexCount : 0;
}

inline void clear_pending_entries() {
    get_pending_history().clear();
    get_pending_choices().clear();
}

inline void add_history_entry(int type, const char* speaker, const char* text,
                              float r, float g, float b, bool selected) {
    DialogueEntry entry;
    entry.type = static_cast<EntryType>(type);
    entry.speaker = speaker ? speaker : "";
    entry.text = text ? text : "";
    entry.speakerR = r; entry.speakerG = g; entry.speakerB = b;
    entry.selected = selected;
    get_pending_history().push_back(entry);
}

inline void add_choice_entry_with_selected(const char* text, bool selected) {
    DialogueEntry entry;
    entry.type = selected ? EntryType::ChoiceSelected : EntryType::Choice;
    entry.text = text ? text : "";
    entry.selected = selected;
    get_pending_choices().push_back(entry);
}

inline void add_choice_entry(const char* text) {
    add_choice_entry_with_selected(text, false);
}

inline void build_dialogue_from_pending() {
    // TODO: Build vertex buffer from pending entries
    // For now just mark as having been called
}

inline int get_pending_history_count() {
    return static_cast<int>(get_pending_history().size());
}

inline int get_pending_choices_count() {
    return static_cast<int>(get_pending_choices().size());
}

inline void update_mouse_position(float x, float y) {
    auto* tr = get_text_renderer();
    if (!tr) return;
    tr->mouseX = x;
    tr->mouseY = y;
    // TODO: Update hovered choice based on position
}

inline int get_hovered_choice() {
    auto* tr = get_text_renderer();
    return tr ? tr->hoveredChoice : -1;
}

inline int get_clicked_choice() {
    // TODO: Check if mouse was clicked on a choice
    return -1;
}

inline bool load_background_image_simple(const char* filepath, const std::string& shaderDir) {
    // TODO: Implement WebGPU background loading
    std::cout << "[fiction-wasm] Background loading not yet implemented: " << filepath << std::endl;
    return false;
}

inline bool init_text_renderer_simple(float screenWidth, float screenHeight, const std::string& shaderDir) {
    // TODO: Initialize WebGPU text rendering
    std::cout << "[fiction-wasm] Text renderer init: " << screenWidth << "x" << screenHeight << std::endl;
    
    auto* tr = new TextRenderer();
    get_text_renderer() = tr;
    tr->screenWidth = screenWidth;
    tr->screenHeight = screenHeight;
    tr->initialized = true;  // Mark as initialized even though not fully working
    
    return true;
}

inline void cleanup_text_renderer() {
    auto* tr = get_text_renderer();
    if (!tr) return;
    
    // TODO: Cleanup WebGPU resources
    
    delete tr;
    get_text_renderer() = nullptr;
}

} // namespace fiction

// =============================================================================
// fiction_engine namespace - Engine control
// =============================================================================

namespace fiction_engine {

// Global WebGPU instance (Dawn requires this)
inline wgpu::Instance& get_instance() {
    static wgpu::Instance instance = wgpuCreateInstance(nullptr);
    return instance;
}

struct Engine {
    wgpu::Adapter adapter = nullptr;
    wgpu::Device device = nullptr;
    wgpu::Queue queue = nullptr;
    wgpu::Surface surface = nullptr;
    
    wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::BGRA8Unorm;
    
    uint32_t canvasWidth = 1280;
    uint32_t canvasHeight = 720;
    
    bool running = true;
    bool initialized = false;
    bool deviceReady = false;
    bool initFailed = false;
    
    // Callback to run after device is ready
    std::function<void()> onReady;
};

inline Engine*& get_engine() {
    static Engine* ptr = nullptr;
    return ptr;
}

// Callback type
typedef void (*RenderCallback)(void*);

inline RenderCallback& get_render_callback() {
    static RenderCallback cb = nullptr;
    return cb;
}

inline void set_render_callback(RenderCallback cb) {
    get_render_callback() = cb;
}

inline uint32_t get_screen_width() {
    auto* e = get_engine();
    return e ? e->canvasWidth : 0;
}

inline uint32_t get_screen_height() {
    auto* e = get_engine();
    return e ? e->canvasHeight : 0;
}

inline float get_pixel_scale() {
    return emscripten_get_device_pixel_ratio();
}

// Event queue
struct InputEvent {
    int type;
    int scancode;
    float scrollY;
    float mouseX, mouseY;
    int mouseButton;
};

inline std::vector<InputEvent>& get_event_queue() {
    static std::vector<InputEvent> queue;
    return queue;
}

inline int get_event_count() {
    return static_cast<int>(get_event_queue().size());
}

inline int get_event_type(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].type;
}

inline int get_event_scancode(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].scancode;
}

inline float get_event_scroll_y(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].scrollY;
}

inline float get_event_mouse_x(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].mouseX;
}

inline float get_event_mouse_y(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].mouseY;
}

inline int get_event_mouse_button(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].mouseButton;
}

// Emscripten callbacks
inline EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN) {
        get_event_queue().push_back({1, (int)e->keyCode, 0, 0, 0, 0});
        if (e->keyCode == 27) { // ESC
            auto* eng = get_engine();
            if (eng) eng->running = false;
        }
    } else if (eventType == EMSCRIPTEN_EVENT_KEYUP) {
        get_event_queue().push_back({2, (int)e->keyCode, 0, 0, 0, 0});
    }
    return EM_TRUE;
}

inline EM_BOOL mouse_callback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    float x = (float)e->targetX;
    float y = (float)e->targetY;
    if (eventType == EMSCRIPTEN_EVENT_MOUSEMOVE) {
        get_event_queue().push_back({4, 0, 0, x, y, 0});
    } else if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN) {
        get_event_queue().push_back({5, 0, 0, x, y, (int)e->button + 1});
    } else if (eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
        get_event_queue().push_back({6, 0, 0, x, y, (int)e->button + 1});
    }
    return EM_TRUE;
}

inline EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent* e, void* userData) {
    get_event_queue().push_back({3, 0, (float)e->deltaY, 0, 0, 0});
    return EM_TRUE;
}

// Configure surface after device is ready
inline void configure_surface() {
    auto* e = get_engine();
    if (!e || !e->device || !e->surface) return;
    
    wgpu::SurfaceConfiguration config{};
    config.device = e->device;
    config.format = e->surfaceFormat;
    config.width = e->canvasWidth;
    config.height = e->canvasHeight;
    config.presentMode = wgpu::PresentMode::Fifo;
    config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    
    e->surface.Configure(&config);
    
    std::cout << "[fiction-wasm] Surface configured: " << e->canvasWidth << "x" << e->canvasHeight << std::endl;
}

// Forward declarations for async WebGPU initialization
inline bool is_device_ready();
inline void wait_for_device();

// Called after device request completes
inline void on_device_ready() {
    auto* e = get_engine();
    if (!e) return;
    
    e->queue = e->device.GetQueue();
    
    // Error callback was already set in DeviceDescriptor
    
    configure_surface();
    
    e->deviceReady = true;
    e->initialized = true;
    
    std::cout << "[fiction-wasm] Device ready, engine initialized" << std::endl;
    
    // Call the ready callback if set
    if (e->onReady) {
        e->onReady();
    }
}

// Called after adapter request completes
inline void on_adapter_ready() {
    auto* e = get_engine();
    if (!e || !e->adapter) return;
    
    std::cout << "[fiction-wasm] Adapter acquired, requesting device..." << std::endl;
    
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType errorType, wgpu::StringView message) {
            std::cerr << "[WebGPU Error " << (int)errorType << "]: " 
                      << std::string(message.data, message.length) << std::endl;
        });
    
    e->adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::AllowProcessEvents,
        [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (message.length) {
                std::cout << "[fiction-wasm] RequestDevice: " 
                          << std::string(message.data, message.length) << std::endl;
            }
            
            if (status != wgpu::RequestDeviceStatus::Success) {
                std::cerr << "[fiction-wasm] Failed to get WebGPU device" << std::endl;
                auto* e = get_engine();
                if (e) {
                    e->initFailed = true;
                    e->running = false;
                }
                return;
            }
            
            auto* e = get_engine();
            if (e) {
                e->device = device;
                on_device_ready();
            }
        });
}

inline bool init(const char* title) {
    if (get_engine() && get_engine()->initialized) {
        return true;
    }
    
    get_engine() = new Engine();
    auto* e = get_engine();
    
    // Get canvas size
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    e->canvasWidth = (uint32_t)cssWidth;
    e->canvasHeight = (uint32_t)cssHeight;
    
    std::cout << "[fiction-wasm] Canvas size: " << e->canvasWidth << "x" << e->canvasHeight << std::endl;
    
    // Create surface from canvas
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
    canvasDesc.selector = "#canvas";
    
    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvasDesc;
    
    e->surface = get_instance().CreateSurface(&surfaceDesc);
    if (!e->surface) {
        std::cerr << "[fiction-wasm] Failed to create surface" << std::endl;
        return false;
    }
    
    // Set up event handlers
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, key_callback);
    emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_mouseup_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, wheel_callback);
    
    // Request adapter (async)
    std::cout << "[fiction-wasm] Requesting WebGPU adapter..." << std::endl;
    
    wgpu::RequestAdapterOptions adapterOpts{};
    adapterOpts.powerPreference = wgpu::PowerPreference::HighPerformance;
    adapterOpts.compatibleSurface = e->surface;
    
    get_instance().RequestAdapter(&adapterOpts, wgpu::CallbackMode::AllowProcessEvents,
        [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (message.length) {
                std::cout << "[fiction-wasm] RequestAdapter: " 
                          << std::string(message.data, message.length) << std::endl;
            }
            
            if (status != wgpu::RequestAdapterStatus::Success) {
                std::cerr << "[fiction-wasm] Failed to get WebGPU adapter" << std::endl;
                auto* e = get_engine();
                if (e) {
                    e->initFailed = true;
                    e->running = false;
                }
                return;
            }
            
            auto* e = get_engine();
            if (e) {
                e->adapter = adapter;
                on_adapter_ready();
            }
        });
    
    // Yield to let async callback fire
    emscripten_sleep(0);
    
    // Wait for device to be ready (async callbacks need browser event loop)
    wait_for_device();
    
    return is_device_ready();
}

inline bool is_device_ready() {
    auto* e = get_engine();
    return e && e->deviceReady;
}

inline void wait_for_device() {
    // Wait for WebGPU device to be ready by polling callbacks
    int maxWait = 1000;  // Max 1000 iterations (10 seconds at 10ms each)
    while (!is_device_ready() && get_engine() && !get_engine()->initFailed && maxWait > 0) {
        // CRITICAL: Must call ProcessEvents to pump the WebGPU callback queue!
        // Without this, AllowSpontaneous/AllowProcessEvents callbacks never fire.
        get_instance().ProcessEvents();
        emscripten_sleep(10);
        maxWait--;
    }
    if (get_engine() && get_engine()->initFailed) {
        std::cerr << "[fiction-wasm] WebGPU initialization failed" << std::endl;
    } else if (!is_device_ready()) {
        std::cerr << "[fiction-wasm] Timeout waiting for WebGPU device" << std::endl;
    }
}

inline void cleanup() {
    auto* e = get_engine();
    if (!e) return;
    
    // C++ wrappers handle release automatically
    e->surface = nullptr;
    e->queue = nullptr;
    e->device = nullptr;
    e->adapter = nullptr;
    
    delete e;
    get_engine() = nullptr;
    
    std::cout << "[fiction-wasm] Engine cleaned up" << std::endl;
}

inline bool should_close() {
    auto* e = get_engine();
    return !e || !e->running;
}

inline void poll_events() {
    get_event_queue().clear();
    // Events are handled asynchronously via callbacks
    
    // Yield to browser event loop - needed for async WebGPU callbacks
    // and to prevent the page from becoming unresponsive
    emscripten_sleep(0);
}

inline void draw_frame() {
    auto* e = get_engine();
    if (!e || !e->deviceReady || !e->surface) return;
    
    // Get current surface texture
    wgpu::SurfaceTexture surfaceTexture;
    e->surface.GetCurrentTexture(&surfaceTexture);
    
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        std::cerr << "[fiction-wasm] Failed to get surface texture" << std::endl;
        return;
    }
    
    wgpu::TextureViewDescriptor viewDesc{};
    wgpu::TextureView backbuffer = surfaceTexture.texture.CreateView(&viewDesc);
    
    wgpu::CommandEncoderDescriptor encoderDesc{};
    wgpu::CommandEncoder encoder = e->device.CreateCommandEncoder(&encoderDesc);
    
    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view = backbuffer;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = {0.05, 0.05, 0.07, 1.0};
    
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
    
    // TODO: Call render callback to draw text, background, etc.
    // if (get_render_callback()) {
    //     get_render_callback()(pass);
    // }
    
    pass.End();
    
    wgpu::CommandBufferDescriptor cmdDesc{};
    wgpu::CommandBuffer commands = encoder.Finish(&cmdDesc);
    e->queue.Submit(1, &commands);

    // Emscripten WebGPU canvas surfaces are presented by the browser.
    // Calling Surface::Present aborts with:
    // "wgpuSurfacePresent is unsupported (use requestAnimationFrame via html5.h instead)".
}

// File I/O - reads from Emscripten's virtual filesystem
inline std::vector<std::string>& get_file_lines() {
    static std::vector<std::string> lines;
    return lines;
}

inline int read_file_lines(const char* filepath) {
    auto& lines = get_file_lines();
    lines.clear();
    
    FILE* f = fopen(filepath, "r");
    if (!f) {
        std::cerr << "[fiction-wasm] Failed to open file: " << filepath << std::endl;
        return 0;
    }
    
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        lines.push_back(buf);
    }
    fclose(f);
    
    return static_cast<int>(lines.size());
}

inline const char* get_file_line(int index) {
    auto& lines = get_file_lines();
    if (index < 0 || index >= (int)lines.size()) return "";
    return lines[index].c_str();
}

inline int64_t get_file_mod_time(const char* path) {
    // In WASM, files don't change at runtime, so always return 0
    return 0;
}

} // namespace fiction_engine

#endif // __EMSCRIPTEN__
