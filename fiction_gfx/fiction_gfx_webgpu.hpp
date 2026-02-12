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
#include <array>
#include <fstream>
#include <iterator>
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

// Global accessors - defined once in fiction_gfx_webgpu.cpp to avoid ODR issues
extern TextRenderer* g_text_renderer;
inline TextRenderer*& get_text_renderer() {
    return g_text_renderer;
}

extern std::vector<DialogueEntry> g_pending_history;
inline std::vector<DialogueEntry>& get_pending_history() {
    return g_pending_history;
}

extern std::vector<DialogueEntry> g_pending_choices;
inline std::vector<DialogueEntry>& get_pending_choices() {
    return g_pending_choices;
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
    extern BackgroundRenderer* g_bg_renderer;
    return g_bg_renderer;
}

struct OverlayRenderer {
    struct FrameTexture {
        wgpu::Texture texture = nullptr;
        wgpu::TextureView textureView = nullptr;
        int width = 0;
        int height = 0;
    };

    wgpu::Sampler sampler = nullptr;

    wgpu::RenderPipeline pipeline = nullptr;
    wgpu::BindGroup bindGroup = nullptr;
    wgpu::BindGroupLayout bindGroupLayout = nullptr;
    wgpu::Buffer uniformBuffer = nullptr;
    wgpu::Buffer vertexBuffer = nullptr;

    std::unordered_map<std::string, FrameTexture> frames;
    std::string activePath;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool initialized = false;
};

inline OverlayRenderer*& get_overlay_renderer() {
    extern OverlayRenderer* g_overlay_renderer;
    return g_overlay_renderer;
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
    struct SpeakerParticleQuad {
        float x, y, w, h;
        float r, g, b;
    };
    std::vector<SpeakerParticleQuad> speakerQuads;
    
    float startTime = 0.0f;
    
    bool initialized = false;
};

inline ParticleRenderer*& get_particle_renderer() {
    extern ParticleRenderer* g_particle_renderer;
    return g_particle_renderer;
}

// =============================================================================
// Font path
// =============================================================================

inline std::string& get_font_path() {
    extern std::string g_font_path;
    return g_font_path;
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
    // WebGPU surface coordinates are top-left origin, so Y must be flipped here.
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
    // WebGPU surface coordinates are top-left origin, so Y must be flipped here.
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

inline bool read_file_to_string(const std::string& path, std::string& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return !out.empty();
}

inline bool load_particle_shader_wgsl(const std::string& shaderDir, std::string& outCode) {
    std::vector<std::string> candidates;
    candidates.push_back(shaderDir + "/particle.wgsl");
    candidates.push_back("/" + shaderDir + "/particle.wgsl");
    candidates.push_back("vulkan_fiction/particle.wgsl");
    candidates.push_back("/vulkan_fiction/particle.wgsl");

    for (const auto& path : candidates) {
        if (read_file_to_string(path, outCode)) {
            std::cout << "[fiction-wasm] Loaded particle WGSL: " << path << std::endl;
            return true;
        }
    }
    std::cerr << "[fiction-wasm] Failed to load particle WGSL shader file" << std::endl;
    return false;
}

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
    // Manual scroll cancels auto-scroll and uses pixel delta directly (Vulkan parity).
    tr->isAutoScrolling = false;
    tr->scrollOffset = std::max(0.0f, std::min(tr->maxScroll, tr->scrollOffset + delta));
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

inline std::vector<int32_t> get_required_codepoints() {
    std::vector<int32_t> codepoints;

    // Basic printable ASCII.
    for (int c = 32; c < 127; c++) {
        codepoints.push_back(c);
    }

    // Latin-1 supplement for accented western European text.
    for (int c = 0x00A0; c <= 0x00FF; c++) {
        codepoints.push_back(c);
    }

    // Extra punctuation/ligatures used in story text and metadata.
    int32_t extras[] = {
        0x0152, 0x0153, 0x0178, // OE/oe/Y diaeresis
        0x2013, 0x2014,         // en/em dash
        0x2018, 0x2019,         // curly single quotes
        0x201C, 0x201D,         // curly double quotes
        0x2026,                 // ellipsis
        0x0394,                 // Delta marker in script
    };
    for (int32_t cp : extras) {
        codepoints.push_back(cp);
    }
    return codepoints;
}

inline int32_t decode_utf8(const std::string& str, size_t& i) {
    if (i >= str.size()) return 0;

    uint8_t c = static_cast<uint8_t>(str[i]);
    if ((c & 0x80) == 0) {
        i++;
        return c;
    }
    auto is_cont = [&](size_t idx) -> bool {
        if (idx >= str.size()) return false;
        return (static_cast<uint8_t>(str[idx]) & 0xC0) == 0x80;
    };

    if ((c & 0xE0) == 0xC0 && i + 1 < str.size() && is_cont(i + 1)) {
        int32_t cp = (c & 0x1F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < str.size() && is_cont(i + 1) && is_cont(i + 2)) {
        int32_t cp = (c & 0x0F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < str.size() &&
        is_cont(i + 1) && is_cont(i + 2) && is_cont(i + 3)) {
        int32_t cp = (c & 0x07) << 18;
        cp |= (static_cast<uint8_t>(str[i + 1]) & 0x3F) << 12;
        cp |= (static_cast<uint8_t>(str[i + 2]) & 0x3F) << 6;
        cp |= (static_cast<uint8_t>(str[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }

    // Fallback for non-UTF-8 bytes (e.g. extended single-byte encodings):
    // consume one byte only to avoid swallowing neighboring letters.
    i++;
    return static_cast<int32_t>(c);
}

inline void add_text_quad(TextRenderer* tr,
                          float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a) {
    if (!tr) return;
    if (tr->vertices.size() + 6 > tr->maxVertices) return;

    TextVertex v;
    v.r = r; v.g = g; v.b = b; v.a = a;

    v.x = x;     v.y = y;     v.u = u0; v.v = v0; tr->vertices.push_back(v);
    v.x = x + w; v.y = y;     v.u = u1; v.v = v0; tr->vertices.push_back(v);
    v.x = x;     v.y = y + h; v.u = u0; v.v = v1; tr->vertices.push_back(v);

    v.x = x + w; v.y = y;     v.u = u1; v.v = v0; tr->vertices.push_back(v);
    v.x = x + w; v.y = y + h; v.u = u1; v.v = v1; tr->vertices.push_back(v);
    v.x = x;     v.y = y + h; v.u = u0; v.v = v1; tr->vertices.push_back(v);
}

inline void add_rect(TextRenderer* tr,
                     float x, float y, float w, float h,
                     float r, float g, float b, float a) {
    // Uses a white patch in the atlas (written during atlas generation).
    add_text_quad(tr, x, y, w, h, 0.0f, 0.0f, 0.01f, 0.01f, r, g, b, a);
}

inline float render_text_string(TextRenderer* tr,
                                const std::string& text,
                                float x, float y,
                                float scale,
                                float r, float g, float b, float a) {
    float cursorX = x;
    size_t i = 0;
    while (i < text.size()) {
        int32_t cp = decode_utf8(text, i);
        if (cp == ' ') {
            cursorX += tr->font.spaceWidth * scale;
            continue;
        }

        auto it = tr->font.glyphs.find(cp);
        if (it == tr->font.glyphs.end()) {
            it = tr->font.glyphs.find('?');
            if (it == tr->font.glyphs.end()) {
                cursorX += tr->font.spaceWidth * scale;
                continue;
            }
        }

        const GlyphInfo& gi = it->second;
        add_text_quad(tr,
                      cursorX + gi.xoffset * scale,
                      y + gi.yoffset * scale,
                      gi.width * scale,
                      gi.height * scale,
                      gi.u0, gi.v0, gi.u1, gi.v1,
                      r, g, b, a);
        cursorX += gi.advance * scale;
    }
    return cursorX;
}

inline float measure_text_width(TextRenderer* tr, const std::string& text, float scale) {
    float width = 0.0f;
    size_t i = 0;
    while (i < text.size()) {
        int32_t cp = decode_utf8(text, i);
        if (cp == ' ') {
            width += tr->font.spaceWidth * scale;
            continue;
        }
        auto it = tr->font.glyphs.find(cp);
        if (it != tr->font.glyphs.end()) {
            width += it->second.advance * scale;
        } else {
            width += tr->font.spaceWidth * scale;
        }
    }
    return width;
}

inline std::vector<std::string> wrap_text(TextRenderer* tr,
                                          const std::string& text,
                                          float maxWidth,
                                          float scale) {
    std::vector<std::string> lines;
    std::string currentLine;
    float currentWidth = 0.0f;

    size_t i = 0;
    while (i < text.size()) {
        size_t wordStart = i;
        while (i < text.size()) {
            size_t save = i;
            int32_t cp = decode_utf8(text, i);
            if (cp == ' ' || cp == '\n') {
                i = save;
                break;
            }
        }
        std::string word = text.substr(wordStart, i - wordStart);
        float wordWidth = measure_text_width(tr, word, scale);

        if (currentWidth + wordWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine = word;
            currentWidth = wordWidth;
        } else {
            if (!currentLine.empty()) {
                currentLine += " ";
                currentWidth += tr->font.spaceWidth * scale;
            }
            currentLine += word;
            currentWidth += wordWidth;
        }

        if (i < text.size()) {
            size_t save = i;
            int32_t cp = decode_utf8(text, i);
            if (cp == '\n') {
                if (!currentLine.empty()) lines.push_back(currentLine);
                currentLine.clear();
                currentWidth = 0.0f;
            } else if (cp != ' ') {
                i = save;
            }
        }
    }

    if (!currentLine.empty()) lines.push_back(currentLine);
    return lines;
}

inline float render_dialogue_entry(TextRenderer* tr,
                                   const DialogueEntry& entry,
                                   float y,
                                   float scale,
                                   bool isHovered = false) {
    float panelStartX = tr->screenWidth * tr->style.panelX + tr->style.panelPadding;
    float textWidth = tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2;
    float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;
    float currentY = y;

    float textR = tr->style.textR;
    float textG = tr->style.textG;
    float textB = tr->style.textB;

    switch (entry.type) {
        case EntryType::Choice:
            if (isHovered) {
                textR = tr->style.choiceHoverR; textG = tr->style.choiceHoverG; textB = tr->style.choiceHoverB;
            } else {
                textR = tr->style.choiceR; textG = tr->style.choiceG; textB = tr->style.choiceB;
            }
            break;
        case EntryType::ChoiceSelected:
            if (isHovered) {
                textR = tr->style.choiceSelectedHoverR; textG = tr->style.choiceSelectedHoverG; textB = tr->style.choiceSelectedHoverB;
            } else {
                textR = tr->style.choiceSelectedR; textG = tr->style.choiceSelectedG; textB = tr->style.choiceSelectedB;
            }
            break;
        case EntryType::Narration:
            textR = tr->style.narrationR; textG = tr->style.narrationG; textB = tr->style.narrationB;
            break;
        default:
            break;
    }

    if (!entry.speaker.empty()) {
        float speakerS = scale * tr->style.speakerScale;
        float speakerLineH = tr->font.lineHeight * speakerS + tr->style.lineSpacing;

        // Match Vulkan visual: small painted square to the left of speaker name.
        const float markerSize = speakerLineH * 0.40f;
        const float markerGap = 8.0f;
        auto* pr = get_particle_renderer();
        if (pr && pr->initialized) {
            ParticleRenderer::SpeakerParticleQuad spq{};
            spq.x = panelStartX - markerGap - markerSize;
            spq.y = currentY + (speakerLineH - markerSize) * 0.5f;
            spq.w = markerSize;
            spq.h = markerSize;
            spq.r = entry.speakerR;
            spq.g = entry.speakerG;
            spq.b = entry.speakerB;
            pr->speakerQuads.push_back(spq);
        } else {
            add_rect(tr,
                     panelStartX - markerGap - markerSize,
                     currentY + (speakerLineH - markerSize) * 0.5f,
                     markerSize, markerSize,
                     entry.speakerR, entry.speakerG, entry.speakerB, 1.0f);
        }

        render_text_string(tr, entry.speaker, panelStartX, currentY, speakerS,
                           entry.speakerR, entry.speakerG, entry.speakerB, 1.0f);
        currentY += speakerLineH;
    }

    auto lines = wrap_text(tr, entry.text, textWidth, scale);
    for (const auto& line : lines) {
        float indent = (entry.type == EntryType::Choice || entry.type == EntryType::ChoiceSelected)
            ? tr->style.choiceIndent
            : 0.0f;
        render_text_string(tr, line, panelStartX + indent, currentY, scale, textR, textG, textB, 1.0f);
        currentY += lineH;
    }

    currentY += tr->style.lineSpacing * 2;
    return currentY;
}

inline void build_dialogue_from_pending() {
    auto* tr = get_text_renderer();
    if (!tr) return;

    tr->vertices.clear();
    tr->choiceBounds.clear();
    tr->vertexCount = 0;
    auto* pr = get_particle_renderer();
    if (pr) {
        pr->speakerQuads.clear();
        pr->vertices.clear();
        pr->vertexCount = 0;
    }

    float panelX = tr->screenWidth * tr->style.panelX;
    float panelW = tr->screenWidth * tr->style.panelWidth;
    float panelH = tr->screenHeight;
    float scale = tr->style.textScale;
    float y = tr->style.panelPadding - tr->scrollOffset;

    // Panel background (transparent by default, set via style).
    add_rect(tr, panelX, 0, panelW, panelH,
             tr->style.bgR, tr->style.bgG, tr->style.bgB, tr->style.bgA);

    for (const auto& entry : get_pending_history()) {
        if (y > tr->screenHeight) break;

        if (y + 200 > 0) {
            y = render_dialogue_entry(tr, entry, y, scale, false);
        } else {
            auto lines = wrap_text(tr, entry.text,
                                   tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2,
                                   scale);
            float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;
            y += (!entry.speaker.empty() ? lineH : 0);
            y += lines.size() * lineH;
            y += tr->style.lineSpacing * 2;
        }
        y += tr->style.lineSpacing * tr->style.entrySpacing;
    }

    if (!get_pending_choices().empty()) {
        float sepY = y + 10.0f;
        add_rect(tr, panelX + tr->style.panelPadding, sepY,
                 panelW - tr->style.panelPadding * 2, 1.0f,
                 0.4f, 0.4f, 0.4f, 0.5f);
        y = sepY + 20.0f;
    }

    tr->hoveredChoice = -1;
    float textWidth = tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2;
    float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;

    float choiceY = y;
    for (size_t i = 0; i < get_pending_choices().size(); i++) {
        std::string numberedText = std::to_string(i + 1) + ". " + get_pending_choices()[i].text;
        auto lines = wrap_text(tr, numberedText, textWidth, scale);
        float entryHeight = lines.size() * lineH + tr->style.lineSpacing * 2;

        if (tr->mouseX >= panelX && tr->mouseX <= panelX + panelW &&
            tr->mouseY >= choiceY && tr->mouseY < choiceY + entryHeight) {
            tr->hoveredChoice = static_cast<int>(i);
        }

        ChoiceBounds bounds;
        bounds.y0 = choiceY;
        bounds.y1 = choiceY + entryHeight;
        bounds.index = static_cast<int>(i);
        tr->choiceBounds.push_back(bounds);
        choiceY += entryHeight;
    }

    int choiceNum = 1;
    for (size_t i = 0; i < get_pending_choices().size(); i++) {
        DialogueEntry numberedChoice = get_pending_choices()[i];
        numberedChoice.text = std::to_string(choiceNum) + ". " + get_pending_choices()[i].text;
        bool isHovered = (tr->hoveredChoice == static_cast<int>(i));
        y = render_dialogue_entry(tr, numberedChoice, y, scale, isHovered);
        choiceNum++;
    }

    tr->maxScroll = std::max(0.0f, y + tr->scrollOffset - tr->screenHeight + tr->style.bottomMargin);
    int currentEntryCount = static_cast<int>(get_pending_history().size() + get_pending_choices().size());
    bool newContentAdded = (currentEntryCount > tr->lastEntryCount);
    tr->lastEntryCount = currentEntryCount;

    if (tr->autoScrollEnabled && tr->maxScroll > 0 && newContentAdded) {
        tr->isAutoScrolling = true;
        tr->targetScrollOffset = tr->maxScroll;
    }
    if (tr->isAutoScrolling) {
        float diff = tr->targetScrollOffset - tr->scrollOffset;
        if (std::abs(diff) > 0.5f) {
            float dt = 1.0f / 60.0f;
            float step = tr->scrollAnimationSpeed * tr->font.lineHeight * tr->style.textScale * dt;
            if (diff > 0.0f) {
                tr->scrollOffset += std::min(diff, step);
            } else {
                tr->scrollOffset -= std::min(std::abs(diff), step);
            }
        } else {
            tr->scrollOffset = tr->targetScrollOffset;
            tr->isAutoScrolling = false;
        }
    }

    tr->vertexCount = static_cast<uint32_t>(tr->vertices.size());
    if (tr->vertexBuffer && tr->vertexCount > 0 && tr->queue) {
        tr->queue.WriteBuffer(tr->vertexBuffer, 0, tr->vertices.data(),
                              tr->vertexCount * sizeof(TextVertex));
    }

    if (pr && pr->initialized && pr->vertexBuffer && tr->queue && !pr->speakerQuads.empty()) {
        for (const auto& spq : pr->speakerQuads) {
            if (pr->vertices.size() + 6 > pr->maxVertices) break;

            float x0 = spq.x;
            float y0 = spq.y;
            float x1 = spq.x + spq.w;
            float y1 = spq.y + spq.h;
            float r = spq.r;
            float g = spq.g;
            float b = spq.b;
            float a = 1.0f;

            pr->vertices.push_back({x0, y0, 0.0f, 0.0f, r, g, b, a});
            pr->vertices.push_back({x1, y0, 1.0f, 0.0f, r, g, b, a});
            pr->vertices.push_back({x0, y1, 0.0f, 1.0f, r, g, b, a});
            pr->vertices.push_back({x1, y0, 1.0f, 0.0f, r, g, b, a});
            pr->vertices.push_back({x1, y1, 1.0f, 1.0f, r, g, b, a});
            pr->vertices.push_back({x0, y1, 0.0f, 1.0f, r, g, b, a});
        }

        pr->vertexCount = static_cast<uint32_t>(pr->vertices.size());
        if (pr->vertexCount > 0) {
            tr->queue.WriteBuffer(pr->vertexBuffer, 0, pr->vertices.data(),
                                  pr->vertexCount * sizeof(TextVertex));
        }
    }
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
}

inline int get_hovered_choice() {
    auto* tr = get_text_renderer();
    return tr ? tr->hoveredChoice : -1;
}

inline int get_clicked_choice() {
    auto* tr = get_text_renderer();
    if (!tr) return -1;

    float panelX = tr->screenWidth * tr->style.panelX;
    float panelW = tr->screenWidth * tr->style.panelWidth;
    if (tr->mouseX < panelX || tr->mouseX > panelX + panelW) {
        return -1;
    }

    for (const auto& bounds : tr->choiceBounds) {
        if (tr->mouseY >= bounds.y0 && tr->mouseY < bounds.y1) {
            return bounds.index;
        }
    }
    return -1;
}

inline void cleanup_background_renderer() {
    auto* bg = get_bg_renderer();
    if (!bg) return;
    delete bg;
    get_bg_renderer() = nullptr;
}

inline void cleanup_overlay_renderer() {
    auto* ov = get_overlay_renderer();
    if (!ov) return;
    delete ov;
    get_overlay_renderer() = nullptr;
}

// Helper to create shader module from WGSL (definition below).
inline wgpu::ShaderModule createShaderModule(wgpu::Device& device, const char* code);
inline wgpu::ShaderModule createShaderModule(wgpu::Device& device, const std::string& code);
inline bool init_particle_renderer(const std::string& shaderDir);
inline void cleanup_particle_renderer();

inline bool load_background_image_simple(const char* filepath, const std::string& shaderDir) {
    (void)shaderDir; // WebGPU backend uses embedded WGSL shaders.

    auto* tr = get_text_renderer();
    if (!tr || !tr->initialized || !tr->device || !tr->queue) {
        std::cerr << "[fiction-wasm] Background load requires initialized text renderer" << std::endl;
        return false;
    }

    // Resolve paths robustly for Emscripten FS.
    std::vector<std::string> candidates;
    if (filepath && filepath[0] != '\0') {
        candidates.emplace_back(filepath);
        if (filepath[0] != '/') {
            candidates.emplace_back(std::string("/") + filepath);
        }
        std::string fp(filepath);
        const size_t lastSlash = fp.find_last_of('/');
        if (lastSlash != std::string::npos) {
            const std::string base = fp.substr(lastSlash + 1);
            candidates.emplace_back(std::string("/resources/fiction/") + base);
            candidates.emplace_back(std::string("resources/fiction/") + base);
        }
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = nullptr;
    std::string loadedPath;
    for (const auto& candidate : candidates) {
        pixels = stbi_load(candidate.c_str(), &w, &h, &channels, 4); // Force RGBA
        if (pixels) {
            loadedPath = candidate;
            break;
        }
    }

    if (!pixels) {
        std::cerr << "[fiction-wasm] Failed to load background image: "
                  << (filepath ? filepath : "(null)") << std::endl;
        return false;
    }

    // Fast path: keep existing pipeline/bindings and only update texture bytes.
    auto* existing = get_bg_renderer();
    if (existing && existing->initialized &&
        existing->imgWidth == w && existing->imgHeight == h && existing->texture) {
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = existing->texture;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};

        wgpu::TexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = static_cast<uint32_t>(w * 4);
        layout.rowsPerImage = static_cast<uint32_t>(h);

        wgpu::Extent3D writeSize{};
        writeSize.width = static_cast<uint32_t>(w);
        writeSize.height = static_cast<uint32_t>(h);
        writeSize.depthOrArrayLayers = 1;

        tr->queue.WriteTexture(&destination, pixels, static_cast<size_t>(w * h * 4), &layout, &writeSize);
        stbi_image_free(pixels);
        return true;
    }

    // First load or dimension change: recreate background resources.
    cleanup_background_renderer();

    auto* bg = new BackgroundRenderer();
    get_bg_renderer() = bg;
    bg->imgWidth = w;
    bg->imgHeight = h;

    // Create texture.
    wgpu::TextureDescriptor texDesc{};
    texDesc.size.width = static_cast<uint32_t>(w);
    texDesc.size.height = static_cast<uint32_t>(h);
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = wgpu::TextureDimension::e2D;
    texDesc.format = wgpu::TextureFormat::RGBA8Unorm;
    texDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    bg->texture = tr->device.CreateTexture(&texDesc);

    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = bg->texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};

    wgpu::TexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(w * 4);
    layout.rowsPerImage = static_cast<uint32_t>(h);

    wgpu::Extent3D writeSize{};
    writeSize.width = static_cast<uint32_t>(w);
    writeSize.height = static_cast<uint32_t>(h);
    writeSize.depthOrArrayLayers = 1;

    tr->queue.WriteTexture(&destination, pixels, static_cast<size_t>(w * h * 4), &layout, &writeSize);
    stbi_image_free(pixels);

    wgpu::TextureViewDescriptor texViewDesc{};
    bg->textureView = bg->texture.CreateView(&texViewDesc);

    wgpu::SamplerDescriptor samplerDesc{};
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    samplerDesc.magFilter = wgpu::FilterMode::Linear;
    samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    bg->sampler = tr->device.CreateSampler(&samplerDesc);

    // Uniform buffer (screen size).
    wgpu::BufferDescriptor uniformBufDesc{};
    uniformBufDesc.size = 16;
    uniformBufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bg->uniformBuffer = tr->device.CreateBuffer(&uniformBufDesc);
    float uniformData[4] = {tr->screenWidth, tr->screenHeight, 0.0f, 0.0f};
    tr->queue.WriteBuffer(bg->uniformBuffer, 0, uniformData, sizeof(uniformData));

    // Full-screen quad vertices.
    const float sw = tr->screenWidth;
    const float sh = tr->screenHeight;
    TextVertex quad[6] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 1, 1, 1, 1},
        {sw,   0.0f, 1.0f, 0.0f, 1, 1, 1, 1},
        {0.0f, sh,   0.0f, 1.0f, 1, 1, 1, 1},
        {sw,   0.0f, 1.0f, 0.0f, 1, 1, 1, 1},
        {sw,   sh,   1.0f, 1.0f, 1, 1, 1, 1},
        {0.0f, sh,   0.0f, 1.0f, 1, 1, 1, 1},
    };

    wgpu::BufferDescriptor vertexBufDesc{};
    vertexBufDesc.size = sizeof(quad);
    vertexBufDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    bg->vertexBuffer = tr->device.CreateBuffer(&vertexBufDesc);
    tr->queue.WriteBuffer(bg->vertexBuffer, 0, quad, sizeof(quad));

    // Bind group layout.
    std::array<wgpu::BindGroupLayoutEntry, 3> layoutEntries{};
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = wgpu::ShaderStage::Vertex;
    layoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    layoutEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
    layoutEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    layoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = layoutEntries.size();
    layoutDesc.entries = layoutEntries.data();
    bg->bindGroupLayout = tr->device.CreateBindGroupLayout(&layoutDesc);

    // Bind group.
    std::array<wgpu::BindGroupEntry, 3> bindGroupEntries{};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = bg->uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = 16;
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].textureView = bg->textureView;
    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].sampler = bg->sampler;

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bg->bindGroupLayout;
    bgDesc.entryCount = bindGroupEntries.size();
    bgDesc.entries = bindGroupEntries.data();
    bg->bindGroup = tr->device.CreateBindGroup(&bgDesc);

    wgpu::ShaderModule shaderModule = createShaderModule(tr->device, BG_SHADER_WGSL);
    if (!shaderModule) {
        std::cerr << "[fiction-wasm] Failed to create background shader module" << std::endl;
        cleanup_background_renderer();
        return false;
    }

    // Pipeline layout.
    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bg->bindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = tr->device.CreatePipelineLayout(&pipelineLayoutDesc);

    // Vertex layout (same as text).
    std::array<wgpu::VertexAttribute, 3> vertexAttrs{};
    vertexAttrs[0].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[0].offset = 0;
    vertexAttrs[0].shaderLocation = 0;
    vertexAttrs[1].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[1].offset = 8;
    vertexAttrs[1].shaderLocation = 1;
    vertexAttrs[2].format = wgpu::VertexFormat::Float32x4;
    vertexAttrs[2].offset = 16;
    vertexAttrs[2].shaderLocation = 2;

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(TextVertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = vertexAttrs.size();
    vertexBufferLayout.attributes = vertexAttrs.data();

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = wgpu::TextureFormat::BGRA8Unorm;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fs_main", strlen("fs_main")};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = {"vs_main", strlen("vs_main")};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    bg->pipeline = tr->device.CreateRenderPipeline(&pipelineDesc);
    if (!bg->pipeline) {
        std::cerr << "[fiction-wasm] Failed to create background pipeline" << std::endl;
        cleanup_background_renderer();
        return false;
    }

    bg->initialized = true;
    std::cout << "[fiction-wasm] Background loaded: " << loadedPath << " (" << w << "x" << h << ")" << std::endl;
    return true;
}

inline bool load_overlay_image_simple(const char* filepath,
                                      const std::string& shaderDir,
                                      float x,
                                      float y,
                                      float width,
                                      float height) {
    (void)shaderDir; // WebGPU backend uses embedded WGSL shaders.

    auto* tr = get_text_renderer();
    if (!tr || !tr->initialized || !tr->device || !tr->queue) {
        std::cerr << "[fiction-wasm] Overlay load requires initialized text renderer" << std::endl;
        return false;
    }
    if (!filepath || filepath[0] == '\0') {
        std::cerr << "[fiction-wasm] Overlay path is empty" << std::endl;
        return false;
    }
    if (width <= 0.0f || height <= 0.0f) {
        std::cerr << "[fiction-wasm] Invalid overlay rect: " << width << "x" << height << std::endl;
        return false;
    }

    auto update_overlay_vertices = [&](OverlayRenderer* ov,
                                       float vx,
                                       float vy,
                                       float vw,
                                       float vh) {
        TextVertex quad[6] = {
            {vx,      vy,      0.0f, 0.0f, 1, 1, 1, 1},
            {vx + vw, vy,      1.0f, 0.0f, 1, 1, 1, 1},
            {vx,      vy + vh, 0.0f, 1.0f, 1, 1, 1, 1},
            {vx + vw, vy,      1.0f, 0.0f, 1, 1, 1, 1},
            {vx + vw, vy + vh, 1.0f, 1.0f, 1, 1, 1, 1},
            {vx,      vy + vh, 0.0f, 1.0f, 1, 1, 1, 1},
        };
        tr->queue.WriteBuffer(ov->vertexBuffer, 0, quad, sizeof(quad));
        ov->x = vx;
        ov->y = vy;
        ov->width = vw;
        ov->height = vh;
    };

    auto* ov = get_overlay_renderer();
    if (!ov) {
        ov = new OverlayRenderer();
        get_overlay_renderer() = ov;
    }

    if (!ov->initialized) {
        wgpu::SamplerDescriptor samplerDesc{};
        samplerDesc.minFilter = wgpu::FilterMode::Linear;
        samplerDesc.magFilter = wgpu::FilterMode::Linear;
        samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
        samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
        ov->sampler = tr->device.CreateSampler(&samplerDesc);

        wgpu::BufferDescriptor uniformBufDesc{};
        uniformBufDesc.size = 16;
        uniformBufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        ov->uniformBuffer = tr->device.CreateBuffer(&uniformBufDesc);
        float uniformData[4] = {tr->screenWidth, tr->screenHeight, 0.0f, 0.0f};
        tr->queue.WriteBuffer(ov->uniformBuffer, 0, uniformData, sizeof(uniformData));

        wgpu::BufferDescriptor vertexBufDesc{};
        vertexBufDesc.size = 6 * sizeof(TextVertex);
        vertexBufDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        ov->vertexBuffer = tr->device.CreateBuffer(&vertexBufDesc);

        std::array<wgpu::BindGroupLayoutEntry, 3> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = wgpu::ShaderStage::Vertex;
        layoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;

        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
        layoutEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        layoutEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

        layoutEntries[2].binding = 2;
        layoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
        layoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

        wgpu::BindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.entryCount = layoutEntries.size();
        layoutDesc.entries = layoutEntries.data();
        ov->bindGroupLayout = tr->device.CreateBindGroupLayout(&layoutDesc);

        wgpu::ShaderModule shaderModule = createShaderModule(tr->device, BG_SHADER_WGSL);
        if (!shaderModule) {
            std::cerr << "[fiction-wasm] Failed to create overlay shader module" << std::endl;
            cleanup_overlay_renderer();
            return false;
        }

        wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &ov->bindGroupLayout;
        wgpu::PipelineLayout pipelineLayout = tr->device.CreatePipelineLayout(&pipelineLayoutDesc);

        std::array<wgpu::VertexAttribute, 3> vertexAttrs{};
        vertexAttrs[0].format = wgpu::VertexFormat::Float32x2;
        vertexAttrs[0].offset = 0;
        vertexAttrs[0].shaderLocation = 0;
        vertexAttrs[1].format = wgpu::VertexFormat::Float32x2;
        vertexAttrs[1].offset = 8;
        vertexAttrs[1].shaderLocation = 1;
        vertexAttrs[2].format = wgpu::VertexFormat::Float32x4;
        vertexAttrs[2].offset = 16;
        vertexAttrs[2].shaderLocation = 2;

        wgpu::VertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(TextVertex);
        vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
        vertexBufferLayout.attributeCount = vertexAttrs.size();
        vertexBufferLayout.attributes = vertexAttrs.data();

        wgpu::BlendState alphaBlend{};
        alphaBlend.color.operation = wgpu::BlendOperation::Add;
        alphaBlend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
        alphaBlend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        alphaBlend.alpha.operation = wgpu::BlendOperation::Add;
        alphaBlend.alpha.srcFactor = wgpu::BlendFactor::One;
        alphaBlend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

        wgpu::ColorTargetState colorTarget{};
        colorTarget.format = wgpu::TextureFormat::BGRA8Unorm;
        colorTarget.blend = &alphaBlend;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState fragmentState{};
        fragmentState.module = shaderModule;
        fragmentState.entryPoint = {"fs_main", strlen("fs_main")};
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        wgpu::RenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.vertex.entryPoint = {"vs_main", strlen("vs_main")};
        pipelineDesc.vertex.bufferCount = 1;
        pipelineDesc.vertex.buffers = &vertexBufferLayout;
        pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
        pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;

        ov->pipeline = tr->device.CreateRenderPipeline(&pipelineDesc);
        if (!ov->pipeline) {
            std::cerr << "[fiction-wasm] Failed to create overlay pipeline" << std::endl;
            cleanup_overlay_renderer();
            return false;
        }
        ov->initialized = true;
    }

    std::vector<std::string> candidates;
    candidates.emplace_back(filepath);
    if (filepath[0] != '/') {
        candidates.emplace_back(std::string("/") + filepath);
    }
    std::string fp(filepath);
    const size_t lastSlash = fp.find_last_of('/');
    if (lastSlash != std::string::npos) {
        const std::string base = fp.substr(lastSlash + 1);
        candidates.emplace_back(std::string("/resources/fiction/anim/voiture/") + base);
        candidates.emplace_back(std::string("resources/fiction/anim/voiture/") + base);
    }

    const std::string pathKey(filepath);
    auto frameIt = ov->frames.find(pathKey);
    if (frameIt == ov->frames.end()) {
        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = nullptr;
        std::string loadedPath;
        for (const auto& candidate : candidates) {
            pixels = stbi_load(candidate.c_str(), &w, &h, &channels, 4);
            if (pixels) {
                loadedPath = candidate;
                break;
            }
        }
        if (!pixels) {
            std::cerr << "[fiction-wasm] Failed to load overlay image: " << filepath << std::endl;
            return false;
        }

        OverlayRenderer::FrameTexture frame{};
        frame.width = w;
        frame.height = h;

        wgpu::TextureDescriptor texDesc{};
        texDesc.size.width = static_cast<uint32_t>(w);
        texDesc.size.height = static_cast<uint32_t>(h);
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = wgpu::TextureDimension::e2D;
        texDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        texDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        frame.texture = tr->device.CreateTexture(&texDesc);

        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = frame.texture;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};

        wgpu::TexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = static_cast<uint32_t>(w * 4);
        layout.rowsPerImage = static_cast<uint32_t>(h);

        wgpu::Extent3D writeSize{};
        writeSize.width = static_cast<uint32_t>(w);
        writeSize.height = static_cast<uint32_t>(h);
        writeSize.depthOrArrayLayers = 1;

        tr->queue.WriteTexture(&destination, pixels, static_cast<size_t>(w * h * 4), &layout, &writeSize);
        stbi_image_free(pixels);

        wgpu::TextureViewDescriptor texViewDesc{};
        frame.textureView = frame.texture.CreateView(&texViewDesc);

        auto inserted = ov->frames.emplace(pathKey, frame);
        frameIt = inserted.first;
        std::cout << "[fiction-wasm] Overlay frame cached: " << loadedPath
                  << " (" << w << "x" << h << ")" << std::endl;
    }

    const float eps = 0.01f;
    if (std::fabs(ov->x - x) > eps ||
        std::fabs(ov->y - y) > eps ||
        std::fabs(ov->width - width) > eps ||
        std::fabs(ov->height - height) > eps) {
        update_overlay_vertices(ov, x, y, width, height);
    }

    if (ov->activePath != pathKey) {
        std::array<wgpu::BindGroupEntry, 3> bindGroupEntries{};
        bindGroupEntries[0].binding = 0;
        bindGroupEntries[0].buffer = ov->uniformBuffer;
        bindGroupEntries[0].offset = 0;
        bindGroupEntries[0].size = 16;
        bindGroupEntries[1].binding = 1;
        bindGroupEntries[1].textureView = frameIt->second.textureView;
        bindGroupEntries[2].binding = 2;
        bindGroupEntries[2].sampler = ov->sampler;

        wgpu::BindGroupDescriptor ovDesc{};
        ovDesc.layout = ov->bindGroupLayout;
        ovDesc.entryCount = bindGroupEntries.size();
        ovDesc.entries = bindGroupEntries.data();
        ov->bindGroup = tr->device.CreateBindGroup(&ovDesc);
        ov->activePath = pathKey;
    }

    return true;
}

inline void clear_overlay_image() {
    cleanup_overlay_renderer();
}

// Helper to create shader module from WGSL
inline wgpu::ShaderModule createShaderModule(wgpu::Device& device, const char* code) {
    wgpu::ShaderSourceWGSL wgslDesc{};
    wgslDesc.code = {code, strlen(code)};
    
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgslDesc;
    
    return device.CreateShaderModule(&desc);
}

inline wgpu::ShaderModule createShaderModule(wgpu::Device& device, const std::string& code) {
    wgpu::ShaderSourceWGSL wgslDesc{};
    wgslDesc.code = {code.c_str(), code.size()};

    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgslDesc;

    return device.CreateShaderModule(&desc);
}

inline void cleanup_particle_renderer() {
    auto* pr = get_particle_renderer();
    if (!pr) return;
    delete pr;
    get_particle_renderer() = nullptr;
}

// Forward declaration - implementation after Engine is defined
bool init_text_renderer_simple_impl(float screenWidth, float screenHeight, const std::string& shaderDir);

inline bool init_text_renderer_simple(float screenWidth, float screenHeight, const std::string& shaderDir) {
    return init_text_renderer_simple_impl(screenWidth, screenHeight, shaderDir);
}

inline void cleanup_text_renderer() {
    auto* tr = get_text_renderer();
    if (!tr) return;
    
    cleanup_particle_renderer();
    cleanup_overlay_renderer();
    cleanup_background_renderer();
    
    delete tr;
    get_text_renderer() = nullptr;
}

} // namespace fiction

// =============================================================================
// fiction_engine namespace - Engine control
// =============================================================================

namespace fiction_engine {

// Global WebGPU instance - defined once in fiction_gfx_webgpu.cpp
extern wgpu::Instance g_instance;
inline wgpu::Instance& get_instance() {
    return g_instance;
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

// Global engine pointer - defined once in fiction_gfx_webgpu.cpp
extern Engine* g_engine_ptr;
inline Engine*& get_engine() {
    return g_engine_ptr;
}

// Callback type
typedef void (*RenderCallback)(void*);

// Global render callback - defined once in fiction_gfx_webgpu.cpp
extern RenderCallback g_render_callback;
inline RenderCallback& get_render_callback() {
    return g_render_callback;
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

// Global event queue - defined once in fiction_gfx_webgpu.cpp to avoid ODR issues
// with multiple translation units getting separate static instances.
extern std::vector<InputEvent> g_event_queue;

inline std::vector<InputEvent>& get_event_queue() {
    return g_event_queue;
}

inline int get_event_count() {
    int count = static_cast<int>(get_event_queue().size());
    return count;
}

inline void clear_events() {
    get_event_queue().clear();
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
inline int translate_web_key_to_scancode(const EmscriptenKeyboardEvent* e) {
    if (!e) return 0;

    // Match SDL scancodes used by fiction.jank:
    // 1..9 -> 30..38, Up -> 82, Down -> 81, Escape -> 41
    if (strcmp(e->code, "Digit1") == 0) return 30;
    if (strcmp(e->code, "Digit2") == 0) return 31;
    if (strcmp(e->code, "Digit3") == 0) return 32;
    if (strcmp(e->code, "Digit4") == 0) return 33;
    if (strcmp(e->code, "Digit5") == 0) return 34;
    if (strcmp(e->code, "Digit6") == 0) return 35;
    if (strcmp(e->code, "Digit7") == 0) return 36;
    if (strcmp(e->code, "Digit8") == 0) return 37;
    if (strcmp(e->code, "Digit9") == 0) return 38;
    if (strcmp(e->code, "Numpad1") == 0) return 30;
    if (strcmp(e->code, "Numpad2") == 0) return 31;
    if (strcmp(e->code, "Numpad3") == 0) return 32;
    if (strcmp(e->code, "Numpad4") == 0) return 33;
    if (strcmp(e->code, "Numpad5") == 0) return 34;
    if (strcmp(e->code, "Numpad6") == 0) return 35;
    if (strcmp(e->code, "Numpad7") == 0) return 36;
    if (strcmp(e->code, "Numpad8") == 0) return 37;
    if (strcmp(e->code, "Numpad9") == 0) return 38;
    if (strcmp(e->code, "ArrowUp") == 0) return 82;
    if (strcmp(e->code, "ArrowDown") == 0) return 81;
    if (strcmp(e->code, "Escape") == 0) return 41;

    // Fallback by key value for layouts/browsers where `code` is missing.
    if (strcmp(e->key, "1") == 0) return 30;
    if (strcmp(e->key, "2") == 0) return 31;
    if (strcmp(e->key, "3") == 0) return 32;
    if (strcmp(e->key, "4") == 0) return 33;
    if (strcmp(e->key, "5") == 0) return 34;
    if (strcmp(e->key, "6") == 0) return 35;
    if (strcmp(e->key, "7") == 0) return 36;
    if (strcmp(e->key, "8") == 0) return 37;
    if (strcmp(e->key, "9") == 0) return 38;
    if (strcmp(e->key, "ArrowUp") == 0 || strcmp(e->key, "Up") == 0) return 82;
    if (strcmp(e->key, "ArrowDown") == 0 || strcmp(e->key, "Down") == 0) return 81;
    if (strcmp(e->key, "Escape") == 0 || strcmp(e->key, "Esc") == 0) return 41;

    // Fallback by legacy keyCode.
    if (e->keyCode >= 49 && e->keyCode <= 57) return 30 + (e->keyCode - 49);
    if (e->keyCode == 38) return 82;
    if (e->keyCode == 40) return 81;
    if (e->keyCode == 27) return 41;

    return static_cast<int>(e->keyCode);
}

inline EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    const int scancode = translate_web_key_to_scancode(e);
    
    if (scancode == 0) return EM_FALSE;

    // When keyboard callbacks are registered on multiple targets (window/document/canvas)
    // the same DOM event can be delivered more than once. Drop near-identical duplicates.
    static int lastType = -1;
    static int lastScancode = -1;
    static double lastTimeMs = 0.0;
    const double nowMs = emscripten_get_now();
    if (eventType == lastType && scancode == lastScancode && (nowMs - lastTimeMs) < 2.0) {
        return EM_TRUE;
    }
    lastType = eventType;
    lastScancode = scancode;
    lastTimeMs = nowMs;

    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN) {
        get_event_queue().push_back({1, scancode, 0, 0, 0, 0});
        if (scancode == 41 || e->keyCode == 27) { // ESC
            auto* eng = get_engine();
            if (eng) eng->running = false;
        }
    } else if (eventType == EMSCRIPTEN_EVENT_KEYUP) {
        get_event_queue().push_back({2, scancode, 0, 0, 0, 0});
    }
    return EM_TRUE;
}

inline EM_BOOL mouse_callback(int eventType, const EmscriptenMouseEvent* e, void* userData) {
    // Convert CSS-space event coordinates into canvas backing-pixel space.
    // This keeps hit testing correct when devicePixelRatio != 1.
    float x = static_cast<float>(e->targetX);
    float y = static_cast<float>(e->targetY);
    auto* eng = get_engine();
    if (eng) {
        double cssW = 0.0, cssH = 0.0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        if (cssW > 0.0) {
            x = x * static_cast<float>(eng->canvasWidth / cssW);
        }
        if (cssH > 0.0) {
            y = y * static_cast<float>(eng->canvasHeight / cssH);
        }
        x = std::max(0.0f, std::min(x, static_cast<float>(eng->canvasWidth)));
        y = std::max(0.0f, std::min(y, static_cast<float>(eng->canvasHeight)));
    }
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
    // Match the desktop input convention used by fiction.jank:
    // positive scroll event should move content down.
    get_event_queue().push_back({3, 0, static_cast<float>(-e->deltaY), 0, 0, 0});
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
    
    // Get canvas size in device pixels for crisp text (retina aware).
    double cssWidth, cssHeight;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    double pixelRatio = emscripten_get_device_pixel_ratio();
    if (pixelRatio <= 0.0) pixelRatio = 1.0;

    e->canvasWidth = static_cast<uint32_t>(cssWidth * pixelRatio);
    e->canvasHeight = static_cast<uint32_t>(cssHeight * pixelRatio);
    if (e->canvasWidth == 0) e->canvasWidth = 1280;
    if (e->canvasHeight == 0) e->canvasHeight = 720;

    emscripten_set_canvas_element_size("#canvas",
                                       static_cast<int>(e->canvasWidth),
                                       static_cast<int>(e->canvasHeight));
    
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
    
    // Make canvas focusable so keyboard events are consistently delivered.
    emscripten_run_script(
        "(function(){"
        "var c=document.getElementById('canvas');"
        "if(!c) return;"
        "c.tabIndex=0;"
        "c.style.outline='none';"
        "if(window && window.focus){ window.focus(); }"
        "c.focus();"
        "c.addEventListener('mousedown', function(){ c.focus(); });"
        "c.addEventListener('wheel', function(){ c.focus(); }, {passive:true});"
        "c.addEventListener('mousemove', function(){ c.focus(); });"
        "})();");

    // Set up event handlers
    // Keyboard: register on window + document + canvas for maximum browser compatibility.
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, key_callback);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, key_callback);
    emscripten_set_keydown_callback("#canvas", nullptr, EM_TRUE, key_callback);
    emscripten_set_keyup_callback("#canvas", nullptr, EM_TRUE, key_callback);
    emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_mouseup_callback("#canvas", nullptr, EM_TRUE, mouse_callback);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, wheel_callback);
    
    std::cout << "[fiction-wasm] Event callbacks registered (keyboard on window/document/canvas)" << std::endl;
    
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
    // Events are handled asynchronously via callbacks.
    // Do not clear here: clearing at poll time can drop events that arrived
    // between frames before jank processes the queue.

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

    // Render background first.
    auto* bg = fiction::get_bg_renderer();
    if (bg && bg->initialized && bg->pipeline) {
        pass.SetPipeline(bg->pipeline);
        pass.SetBindGroup(0, bg->bindGroup);
        pass.SetVertexBuffer(0, bg->vertexBuffer, 0, 6 * sizeof(fiction::TextVertex));
        pass.Draw(6);
    }

    // Render animation overlay over background.
    auto* ov = fiction::get_overlay_renderer();
    if (ov && ov->initialized && ov->pipeline && ov->bindGroup && !ov->activePath.empty()) {
        pass.SetPipeline(ov->pipeline);
        pass.SetBindGroup(0, ov->bindGroup);
        pass.SetVertexBuffer(0, ov->vertexBuffer, 0, 6 * sizeof(fiction::TextVertex));
        pass.Draw(6);
    }

    // Render animated speaker particles between background and text.
    auto* pr = fiction::get_particle_renderer();
    if (pr && pr->initialized && pr->pipeline && pr->vertexCount > 0) {
        auto* tr = fiction::get_text_renderer();
        float elapsed = static_cast<float>(emscripten_get_now() * 0.001) - pr->startTime;
        float uniforms[4] = {
            tr ? tr->screenWidth : static_cast<float>(e->canvasWidth),
            tr ? tr->screenHeight : static_cast<float>(e->canvasHeight),
            elapsed,
            -1.0f
        };
        e->queue.WriteBuffer(pr->uniformBuffer, 0, uniforms, sizeof(uniforms));

        pass.SetPipeline(pr->pipeline);
        pass.SetBindGroup(0, pr->bindGroup);
        pass.SetVertexBuffer(0, pr->vertexBuffer, 0, pr->vertexCount * sizeof(fiction::TextVertex));
        pass.Draw(pr->vertexCount);
    }

    // Render text on top.
    auto* tr = fiction::get_text_renderer();
    if (tr && tr->textPipeline && tr->vertexCount > 0) {
        pass.SetPipeline(tr->textPipeline);
        pass.SetBindGroup(0, tr->textBindGroup);
        pass.SetVertexBuffer(0, tr->vertexBuffer, 0, tr->vertexCount * sizeof(fiction::TextVertex));
        pass.Draw(tr->vertexCount);
    }
    
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

inline int64_t get_monotonic_time_ms() {
    return static_cast<int64_t>(emscripten_get_now());
}

inline int normalize_voice_prefixed_file(const char* locale_dir, const char* line_id, const char* extension) {
    // WASM embedded filesystem is immutable at runtime.
    (void)locale_dir;
    (void)line_id;
    (void)extension;
    return 0;
}

inline int normalize_voice_prefixed_files(const char* locale_dir) {
    // WASM embedded filesystem is immutable at runtime.
    (void)locale_dir;
    return 0;
}

} // namespace fiction_engine

// =============================================================================
// Implementation of init_text_renderer_simple_impl (needs access to Engine)
// =============================================================================

namespace fiction {

inline bool init_particle_renderer(const std::string& shaderDir) {
    auto* e = fiction_engine::get_engine();
    auto* tr = get_text_renderer();
    if (!e || !e->deviceReady || !tr) {
        return false;
    }

    std::string shaderCode;
    if (!load_particle_shader_wgsl(shaderDir, shaderCode)) {
        return false;
    }

    auto* pr = new ParticleRenderer();
    get_particle_renderer() = pr;
    pr->startTime = static_cast<float>(emscripten_get_now() * 0.001);

    wgpu::BufferDescriptor uniformDesc{};
    uniformDesc.size = 16;  // vec2 screenSize + time + yFlip
    uniformDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    pr->uniformBuffer = e->device.CreateBuffer(&uniformDesc);

    wgpu::BufferDescriptor vertexDesc{};
    vertexDesc.size = pr->maxVertices * sizeof(TextVertex);
    vertexDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    pr->vertexBuffer = e->device.CreateBuffer(&vertexDesc);

    wgpu::BindGroupLayoutEntry layoutEntry{};
    layoutEntry.binding = 0;
    layoutEntry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    layoutEntry.buffer.type = wgpu::BufferBindingType::Uniform;

    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    pr->bindGroupLayout = e->device.CreateBindGroupLayout(&layoutDesc);

    wgpu::BindGroupEntry bindEntry{};
    bindEntry.binding = 0;
    bindEntry.buffer = pr->uniformBuffer;
    bindEntry.offset = 0;
    bindEntry.size = 16;

    wgpu::BindGroupDescriptor bindDesc{};
    bindDesc.layout = pr->bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    pr->bindGroup = e->device.CreateBindGroup(&bindDesc);

    wgpu::ShaderModule shaderModule = createShaderModule(e->device, shaderCode);
    if (!shaderModule) {
        std::cerr << "[fiction-wasm] Failed to create particle shader module" << std::endl;
        cleanup_particle_renderer();
        return false;
    }

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &pr->bindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = e->device.CreatePipelineLayout(&pipelineLayoutDesc);

    std::array<wgpu::VertexAttribute, 3> vertexAttrs{};
    vertexAttrs[0].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[0].offset = 0;
    vertexAttrs[0].shaderLocation = 0;
    vertexAttrs[1].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[1].offset = 8;
    vertexAttrs[1].shaderLocation = 1;
    vertexAttrs[2].format = wgpu::VertexFormat::Float32x4;
    vertexAttrs[2].offset = 16;
    vertexAttrs[2].shaderLocation = 2;

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(TextVertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = vertexAttrs.size();
    vertexBufferLayout.attributes = vertexAttrs.data();

    wgpu::BlendComponent blendColor{};
    blendColor.operation = wgpu::BlendOperation::Add;
    blendColor.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendColor.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    wgpu::BlendComponent blendAlpha{};
    blendAlpha.operation = wgpu::BlendOperation::Add;
    blendAlpha.srcFactor = wgpu::BlendFactor::One;
    blendAlpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    wgpu::BlendState blendState{};
    blendState.color = blendColor;
    blendState.alpha = blendAlpha;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = e->surfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fs_main", strlen("fs_main")};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = {"vs_main", strlen("vs_main")};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pr->pipeline = e->device.CreateRenderPipeline(&pipelineDesc);
    if (!pr->pipeline) {
        std::cerr << "[fiction-wasm] Failed to create particle pipeline" << std::endl;
        cleanup_particle_renderer();
        return false;
    }

    pr->initialized = true;
    std::cout << "[fiction-wasm] Particle renderer initialized" << std::endl;
    return true;
}

inline bool init_text_renderer_simple_impl(float screenWidth, float screenHeight, const std::string& shaderDir) {
    auto* e = fiction_engine::get_engine();
    if (!e || !e->deviceReady) {
        std::cerr << "[fiction-wasm] Cannot init text renderer: device not ready" << std::endl;
        return false;
    }
    
    // Create text renderer
    auto* tr = new TextRenderer();
    get_text_renderer() = tr;
    
    tr->device = e->device;
    tr->queue = e->queue;
    tr->screenWidth = screenWidth;
    tr->screenHeight = screenHeight;
    
    std::cout << "[fiction-wasm] Initializing text renderer: " << screenWidth << "x" << screenHeight << std::endl;
    
    // Load font file from embedded FS. Try a few deterministic candidates.
    const std::string configuredFont = get_font_path();
    std::string baseName = configuredFont;
    const size_t lastSlash = baseName.find_last_of('/');
    if (lastSlash != std::string::npos) {
        baseName = baseName.substr(lastSlash + 1);
    }

    std::vector<std::string> fontCandidates;
    fontCandidates.push_back("/fonts/" + baseName);
    fontCandidates.push_back("/fonts/" + configuredFont);
    fontCandidates.push_back(configuredFont);
    fontCandidates.push_back("fonts/" + baseName);

    FILE* f = nullptr;
    std::string fontPath;
    for (const auto& candidate : fontCandidates) {
        f = fopen(candidate.c_str(), "rb");
        if (f) {
            fontPath = candidate;
            break;
        }
    }

    if (!f) {
        std::cerr << "[fiction-wasm] Failed to load font. Configured: " << configuredFont << std::endl;
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    size_t fontFileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    tr->font.fontData.resize(fontFileSize);
    fread(tr->font.fontData.data(), 1, fontFileSize, f);
    fclose(f);
    
    std::cout << "[fiction-wasm] Font loaded: " << fontFileSize << " bytes" << std::endl;
    
    // Initialize stb_truetype
    if (!stbtt_InitFont(&tr->font.fontInfo, tr->font.fontData.data(), 0)) {
        std::cerr << "[fiction-wasm] Failed to parse font" << std::endl;
        return false;
    }
    
    // Match Vulkan font sizing for better readability/crispness.
    float fontSize = 32.0f;
    tr->font.scale = stbtt_ScaleForPixelHeight(&tr->font.fontInfo, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&tr->font.fontInfo, &ascent, &descent, &lineGap);
    tr->font.ascent = ascent * tr->font.scale;
    tr->font.descent = descent * tr->font.scale;
    tr->font.lineHeight = (ascent - descent + lineGap) * tr->font.scale;
    
    std::cout << "[fiction-wasm] Font metrics: ascent=" << tr->font.ascent 
              << " descent=" << tr->font.descent 
              << " lineHeight=" << tr->font.lineHeight << std::endl;
    
    // Create font atlas bitmap
    const uint32_t atlasW = tr->font.atlasWidth;
    const uint32_t atlasH = tr->font.atlasHeight;
    std::vector<uint8_t> atlasBitmap(atlasW * atlasH, 0);

    // Reserve a small solid white block at top-left for rectangle rendering.
    for (uint32_t py = 0; py < 32 && py < atlasH; py++) {
        for (uint32_t px = 0; px < 32 && px < atlasW; px++) {
            atlasBitmap[py * atlasW + px] = 255;
        }
    }

    // Pack required glyphs into atlas.
    int penX = 32;
    int penY = 0;
    int rowHeight = 0;
    const int padding = 2;
    int glyphsRendered = 0;

    const auto codepoints = get_required_codepoints();
    for (int32_t cp : codepoints) {
        if (cp == ' ') {
            GlyphInfo info{};
            info.u0 = 0.0f; info.v0 = 0.0f; info.u1 = 0.01f; info.v1 = 0.01f;
            info.width = 0.0f;
            info.height = 0.0f;
            info.advance = tr->font.spaceWidth;
            info.xoffset = 0.0f;
            info.yoffset = 0.0f;
            tr->font.glyphs[cp] = info;
            continue;
        }

        int glyphIndex = stbtt_FindGlyphIndex(&tr->font.fontInfo, cp);
        if (glyphIndex == 0 && cp != 0) {
            continue;
        }

        int advanceWidth = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&tr->font.fontInfo, cp, &advanceWidth, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&tr->font.fontInfo, cp, tr->font.scale, tr->font.scale,
                                    &x0, &y0, &x1, &y1);
        int gw = x1 - x0;
        int gh = y1 - y0;

        if (gw <= 0 || gh <= 0) {
            GlyphInfo info{};
            info.u0 = 0.0f; info.v0 = 0.0f; info.u1 = 0.0f; info.v1 = 0.0f;
            info.width = 0.0f;
            info.height = 0.0f;
            info.advance = advanceWidth * tr->font.scale;
            info.xoffset = 0.0f;
            info.yoffset = 0.0f;
            tr->font.glyphs[cp] = info;
            continue;
        }

        if (penX + gw + padding > static_cast<int>(atlasW)) {
            penX = 0;
            penY += rowHeight + padding;
            rowHeight = 0;
        }
        if (penY + gh > static_cast<int>(atlasH)) {
            std::cerr << "[fiction-wasm] Font atlas full at codepoint " << cp << std::endl;
            break;
        }

        stbtt_MakeCodepointBitmap(&tr->font.fontInfo,
                                  atlasBitmap.data() + penY * atlasW + penX,
                                  gw, gh,
                                  atlasW,
                                  tr->font.scale, tr->font.scale,
                                  cp);

        GlyphInfo info{};
        info.u0 = static_cast<float>(penX) / atlasW;
        info.v0 = static_cast<float>(penY) / atlasH;
        info.u1 = static_cast<float>(penX + gw) / atlasW;
        info.v1 = static_cast<float>(penY + gh) / atlasH;
        info.width = static_cast<float>(gw);
        info.height = static_cast<float>(gh);
        info.advance = advanceWidth * tr->font.scale;
        info.xoffset = static_cast<float>(x0);
        info.yoffset = static_cast<float>(y0) + tr->font.ascent;
        tr->font.glyphs[cp] = info;

        penX += gw + padding;
        rowHeight = std::max(rowHeight, gh);
        glyphsRendered++;
    }
    
    // Estimate space width
    int spaceAdvance, dummy;
    stbtt_GetCodepointHMetrics(&tr->font.fontInfo, ' ', &spaceAdvance, &dummy);
    tr->font.spaceWidth = spaceAdvance * tr->font.scale;
    
    std::cout << "[fiction-wasm] Packed " << glyphsRendered << " glyphs into " 
              << atlasW << "x" << atlasH << " atlas" << std::endl;
    
    // Create WebGPU texture for font atlas
    wgpu::TextureDescriptor texDesc{};
    texDesc.size.width = atlasW;
    texDesc.size.height = atlasH;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = wgpu::TextureDimension::e2D;
    texDesc.format = wgpu::TextureFormat::R8Unorm;
    texDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    
    tr->font.texture = e->device.CreateTexture(&texDesc);
    
    // Upload atlas to texture
    wgpu::TexelCopyTextureInfo destination{};
    destination.texture = tr->font.texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    
    wgpu::TexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = atlasW;
    layout.rowsPerImage = atlasH;
    
    wgpu::Extent3D writeSize = {atlasW, atlasH, 1};
    
    e->queue.WriteTexture(&destination, atlasBitmap.data(), atlasBitmap.size(), &layout, &writeSize);
    
    // Create texture view
    wgpu::TextureViewDescriptor viewDesc{};
    tr->font.textureView = tr->font.texture.CreateView(&viewDesc);
    
    // Create sampler
    wgpu::SamplerDescriptor samplerDesc{};
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    samplerDesc.magFilter = wgpu::FilterMode::Linear;
    samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    
    tr->font.sampler = e->device.CreateSampler(&samplerDesc);
    
    std::cout << "[fiction-wasm] Font atlas texture created" << std::endl;
    
    // Create uniform buffer (for screen size)
    wgpu::BufferDescriptor uniformBufDesc{};
    uniformBufDesc.size = 16;  // vec2<f32> screenSize + padding
    uniformBufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    
    tr->uniformBuffer = e->device.CreateBuffer(&uniformBufDesc);
    
    // Upload screen size
    float uniformData[4] = {screenWidth, screenHeight, 0.0f, 0.0f};
    e->queue.WriteBuffer(tr->uniformBuffer, 0, uniformData, sizeof(uniformData));
    
    // Create vertex buffer
    wgpu::BufferDescriptor vertexBufDesc{};
    vertexBufDesc.size = tr->maxVertices * sizeof(TextVertex);
    vertexBufDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    
    tr->vertexBuffer = e->device.CreateBuffer(&vertexBufDesc);
    
    std::cout << "[fiction-wasm] Buffers created" << std::endl;
    
    // Create bind group layout
    std::array<wgpu::BindGroupLayoutEntry, 3> layoutEntries{};
    
    // Uniform buffer
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = wgpu::ShaderStage::Vertex;
    layoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    
    // Texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    layoutEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
    layoutEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    
    // Sampler
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    layoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
    
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = layoutEntries.size();
    layoutDesc.entries = layoutEntries.data();
    
    tr->textBindGroupLayout = e->device.CreateBindGroupLayout(&layoutDesc);
    
    // Create bind group
    std::array<wgpu::BindGroupEntry, 3> bindGroupEntries{};
    
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = tr->uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = 16;
    
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].textureView = tr->font.textureView;
    
    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].sampler = tr->font.sampler;
    
    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = tr->textBindGroupLayout;
    bgDesc.entryCount = bindGroupEntries.size();
    bgDesc.entries = bindGroupEntries.data();
    
    tr->textBindGroup = e->device.CreateBindGroup(&bgDesc);
    
    std::cout << "[fiction-wasm] Bind group created" << std::endl;
    
    // Create shader module
    wgpu::ShaderModule shaderModule = createShaderModule(e->device, TEXT_SHADER_WGSL);
    if (!shaderModule) {
        std::cerr << "[fiction-wasm] Failed to create shader module" << std::endl;
        return false;
    }
    
    // Create pipeline layout
    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &tr->textBindGroupLayout;
    
    wgpu::PipelineLayout pipelineLayout = e->device.CreatePipelineLayout(&pipelineLayoutDesc);
    
    // Vertex attributes
    std::array<wgpu::VertexAttribute, 3> vertexAttrs{};
    
    // position: vec2<f32>
    vertexAttrs[0].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[0].offset = 0;
    vertexAttrs[0].shaderLocation = 0;
    
    // texCoord: vec2<f32>
    vertexAttrs[1].format = wgpu::VertexFormat::Float32x2;
    vertexAttrs[1].offset = 8;
    vertexAttrs[1].shaderLocation = 1;
    
    // color: vec4<f32>
    vertexAttrs[2].format = wgpu::VertexFormat::Float32x4;
    vertexAttrs[2].offset = 16;
    vertexAttrs[2].shaderLocation = 2;
    
    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(TextVertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = vertexAttrs.size();
    vertexBufferLayout.attributes = vertexAttrs.data();
    
    // Color target with alpha blending
    wgpu::BlendComponent blendColor{};
    blendColor.operation = wgpu::BlendOperation::Add;
    blendColor.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendColor.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    
    wgpu::BlendComponent blendAlpha{};
    blendAlpha.operation = wgpu::BlendOperation::Add;
    blendAlpha.srcFactor = wgpu::BlendFactor::One;
    blendAlpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    
    wgpu::BlendState blendState{};
    blendState.color = blendColor;
    blendState.alpha = blendAlpha;
    
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = e->surfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    
    // Fragment state
    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fs_main", strlen("fs_main")};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    
    // Create render pipeline
    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = {"vs_main", strlen("vs_main")};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    
    tr->textPipeline = e->device.CreateRenderPipeline(&pipelineDesc);
    
    if (!tr->textPipeline) {
        std::cerr << "[fiction-wasm] Failed to create render pipeline" << std::endl;
        return false;
    }
    
    std::cout << "[fiction-wasm] Text render pipeline created" << std::endl;

    if (!init_particle_renderer(shaderDir)) {
        std::cout << "[fiction-wasm] Particle renderer disabled (shader unavailable)" << std::endl;
    }
    
    tr->initialized = true;
    return true;
}

} // namespace fiction

#endif // __EMSCRIPTEN__
