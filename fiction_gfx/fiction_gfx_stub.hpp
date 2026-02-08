// Fiction Graphics — Stub Header for JIT/AOT
//
// This header provides declarations for all fiction_engine and fiction
// namespace functions without any real backend dependencies.
// Used during jank JIT compilation for WASM AOT code generation.
//
// The actual implementations are provided by:
// - libfiction_gfx_stub.dylib during native JIT/AOT generation
// - fiction_gfx_webgpu.hpp when compiled with Emscripten

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace fiction {

struct GlyphInfo {
    float u0, v0, u1, v1;
    float width, height, advance, xoffset, yoffset;
};

struct FontAtlas {
    uint32_t atlasWidth = 512;
    uint32_t atlasHeight = 512;
    float lineHeight = 24.0f;
    float spaceWidth = 8.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float scale = 0.0f;
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

struct ShaderWatch {
    std::string path;
    int64_t lastMod;
    ShaderWatch() : lastMod(0) {}
    ShaderWatch(ShaderWatch&&) = default;
    ShaderWatch& operator=(ShaderWatch&&) = default;
};

struct TextRenderer {
    FontAtlas font;
    uint32_t maxVertices = 65536;
    uint32_t vertexCount = 0;
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
    std::vector<ShaderWatch> shaderWatches;
    bool initialized = false;
};

// Global accessor
inline TextRenderer*& get_text_renderer() {
    static TextRenderer* ptr = nullptr;
    return ptr;
}

// Text renderer functions - declared but not implemented here
// Implementations provided by libfiction_gfx_stub.dylib at JIT time
bool init_text_renderer_simple(float screenWidth, float screenHeight, const std::string& fontPath);
void cleanup_text_renderer();
bool text_renderer_initialized();
void scroll_dialogue(float delta);
void scroll_to_bottom(bool animate = true);
void set_panel_colors(float r, float g, float b, float a);
void set_panel_position(float x, float width);
void set_text_scale(float scale);
void set_speaker_scale(float scale);
void set_line_spacing(float spacing);
void set_entry_spacing(float spacing);
void set_panel_padding(float padding);
void set_choice_indent(float indent);
void set_text_color(float r, float g, float b);
void set_narration_color(float r, float g, float b);
void set_choice_color(float r, float g, float b);
void set_choice_hover_color(float r, float g, float b);
void set_choice_selected_color(float r, float g, float b);
void set_choice_selected_hover_color(float r, float g, float b);
uint32_t get_text_vertex_count();
void clear_pending_entries();
void add_history_entry(int type, const char* speaker, const char* text, float r, float g, float b, bool selected);
void add_choice_entry_with_selected(const char* text, bool selected);
void add_choice_entry(const char* text);
void build_dialogue_from_pending();
int get_pending_history_count();
int get_pending_choices_count();
void update_mouse_position(float x, float y);
int get_hovered_choice();
int get_clicked_choice();

// Background loader
bool load_background_image_simple(const char* id, const std::string& path);

} // namespace fiction

namespace fiction_engine {

// Engine functions - declared but not implemented here
bool init(const char* title);
void cleanup();
bool should_close();
void poll_events();
void draw_frame();

typedef void (*RenderCallback)(void*);
void set_render_callback(RenderCallback callback);

uint32_t get_screen_width();
uint32_t get_screen_height();
float get_pixel_scale();

// Event queue
int get_event_count();
int get_event_type(int index);
int get_event_scancode(int index);
float get_event_scroll_y(int index);
float get_event_mouse_x(int index);
float get_event_mouse_y(int index);
int get_event_mouse_button(int index);

// File I/O
int read_file_lines(const char* path);
const char* get_file_line(int index);
int64_t get_file_mod_time(const char* path);

} // namespace fiction_engine
