// Fiction Graphics — Native Stub for WASM AOT
//
// Provides all fiction_engine and fiction namespace functions with dummy
// implementations. This is compiled natively and loaded by the jank JIT
// during AOT code generation so symbols can be resolved.
//
// The actual WebGPU implementation in fiction_gfx_webgpu.hpp is only
// compiled for Emscripten.
//
// IMPORTANT: Functions must NOT be inline so they are exported from the dylib!

#include "fiction_gfx_stub.hpp"

namespace fiction {

// Text renderer functions - stubs
bool init_text_renderer_simple(float, float, const std::string&) { return false; }
void cleanup_text_renderer() {}
bool text_renderer_initialized() { return false; }
void scroll_dialogue(float) {}
void scroll_to_bottom(bool) {}
void set_panel_colors(float, float, float, float) {}
void set_panel_position(float, float) {}
void set_text_scale(float) {}
void set_speaker_scale(float) {}
void set_line_spacing(float) {}
void set_entry_spacing(float) {}
void set_panel_padding(float) {}
void set_choice_indent(float) {}
void set_text_color(float, float, float) {}
void set_narration_color(float, float, float) {}
void set_choice_color(float, float, float) {}
void set_choice_hover_color(float, float, float) {}
void set_choice_selected_color(float, float, float) {}
void set_choice_selected_hover_color(float, float, float) {}
uint32_t get_text_vertex_count() { return 0; }
void clear_pending_entries() {}
void add_history_entry(int, const char*, const char*, float, float, float, bool) {}
void add_choice_entry_with_selected(const char*, bool) {}
void add_choice_entry(const char*) {}
void build_dialogue_from_pending() {}
int get_pending_history_count() { return 0; }
int get_pending_choices_count() { return 0; }
void update_mouse_position(float, float) {}
int get_hovered_choice() { return -1; }
int get_clicked_choice() { return -1; }

// Background loader
bool load_background_image_simple(const char*, const std::string&) { return false; }
bool load_overlay_image_simple(const char*, const std::string&, float, float, float, float) { return false; }
void clear_overlay_image() {}

} // namespace fiction

namespace fiction_engine {

// Engine functions - stubs
bool init(const char*) { return false; }
void cleanup() {}
bool should_close() { return true; }
void poll_events() {}
void draw_frame() {}

void set_render_callback(RenderCallback) {}

uint32_t get_screen_width() { return 0; }
uint32_t get_screen_height() { return 0; }
float get_pixel_scale() { return 1.0f; }

// Event queue
int get_event_count() { return 0; }
void clear_events() {}
int get_event_type(int) { return 0; }
int get_event_scancode(int) { return 0; }
float get_event_scroll_y(int) { return 0.0f; }
float get_event_mouse_x(int) { return 0.0f; }
float get_event_mouse_y(int) { return 0.0f; }
int get_event_mouse_button(int) { return 0; }

// File I/O
int read_file_lines(const char*) { return 0; }
const char* get_file_line(int) { return ""; }
int64_t get_file_mod_time(const char*) { return 0; }
int64_t get_monotonic_time_ms() { return 0; }
int normalize_voice_prefixed_file(const char*, const char*, const char*) { return 0; }
int normalize_voice_prefixed_files(const char*) { return 0; }

} // namespace fiction_engine
