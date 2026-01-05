// Drawing Canvas - Minimal native layer for 2D stroke rendering
// RULE: Only put here what CANNOT be done in jank!
// - GPU triangle submission (via SDL_Renderer)
// - SDL window/event handling
// - Touch pressure capture
//
// Everything else (bezier math, tessellation, state) is in jank!
//
// NOTE: Using SDL_Renderer for simplicity - uses Metal on macOS/iOS automatically
// GPU STAMP RENDERING: Optional Metal-based stamp renderer for Procreate-style smooth strokes

#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <iostream>

// Metal stamp renderer (GPU-accelerated drawing)
// Uses extern "C" functions for JIT compatibility
#include "metal_renderer.h"

// iOS logging
#ifdef __APPLE__
#include <os/log.h>
#define TOUCH_LOG(fmt, ...) os_log_info(OS_LOG_DEFAULT, fmt, ##__VA_ARGS__)
#else
#define TOUCH_LOG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#endif

namespace dc {

// =============================================================================
// Constants
// =============================================================================

const int MAX_VERTICES = 100000;  // Max vertices per frame
const int MAX_EVENTS = 64;

// =============================================================================
// Vertex format: position (2D) + color (RGBA)
// =============================================================================

struct Vertex2D {
    float x, y;
    float r, g, b, a;
};

// =============================================================================
// Touch/Mouse event data
// =============================================================================

struct InputEvent {
    int type;        // 0=down, 1=move, 2=up
    float x, y;      // Screen position
    float pressure;  // 0.0-1.0 (Apple Pencil or simulated)
    bool is_pencil;  // true if Apple Pencil, false if finger/mouse
};

// =============================================================================
// Canvas state (ODR-safe heap pointer pattern)
// =============================================================================

struct CanvasState {
    // SDL
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int width = 1024;           // Window size (logical pixels)
    int height = 768;
    int render_width = 1024;    // Render output size (physical pixels for high-DPI)
    int render_height = 768;
    bool running = true;

    // Offscreen canvas texture (for caching finished strokes)
    SDL_Texture* canvas_texture = nullptr;
    bool canvas_dirty = true;  // Need to clear on first use

    // Vertex buffer (CPU-side, we'll render triangles via SDL)
    std::vector<Vertex2D> vertices;

    // Input events for current frame
    InputEvent events[MAX_EVENTS];
    int eventCount = 0;

    // Background color
    float bgColor[4] = {0.95f, 0.95f, 0.92f, 1.0f};  // Off-white paper color

    // Frame timing (for performance measurement)
    uint64_t frame_start_time = 0;
    uint64_t last_frame_time = 0;  // Time for last complete frame (ns)
    double avg_frame_time = 0.0;   // Rolling average (ms)
    int frame_count = 0;

    // Touch tracking (to handle only one finger at a time)
    SDL_FingerID activeFingerID = 0;
    bool touchActive = false;

    bool initialized = false;

    // Metal stamp renderer (GPU-accelerated drawing)
    bool use_metal_renderer = false;  // Toggle for Metal vs CPU rendering
};

// ODR-safe singleton pattern
inline CanvasState** get_state_ptr() {
    static CanvasState* state = nullptr;
    return &state;
}

inline CanvasState* get_state() {
    return *get_state_ptr();
}

// =============================================================================
// Initialization
// =============================================================================

inline bool init(int width, int height, const char* title) {
    // Allocate state on heap
    auto** ptr = get_state_ptr();
    if (*ptr) {
        std::cerr << "[drawing_canvas] Already initialized!" << std::endl;
        return false;
    }
    *ptr = new CanvasState();
    auto* s = *ptr;

    s->width = width;
    s->height = height;

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "[drawing_canvas] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create window
    // Note: SDL_WINDOW_METAL is needed for direct Metal rendering but conflicts with SDL_CreateRenderer
    // On iOS, we use Metal-only rendering in drawing_mobile_ios.mm which creates its own window
    // On macOS desktop, we use SDL_Renderer for now (which uses Metal internally)
    s->window = SDL_CreateWindow(title, width, height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!s->window) {
        std::cerr << "[drawing_canvas] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create renderer (uses Metal on macOS/iOS automatically)
    s->renderer = SDL_CreateRenderer(s->window, nullptr);
    if (!s->renderer) {
        std::cerr << "[drawing_canvas] SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(s->window);
        return false;
    }

    // Enable blending for transparency
    SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_BLEND);

    // Get actual render output size (may differ from window size on high-DPI)
    SDL_GetRenderOutputSize(s->renderer, &s->render_width, &s->render_height);
    std::cout << "[drawing_canvas] Window size: " << s->width << "x" << s->height
              << ", Render output size: " << s->render_width << "x" << s->render_height << std::endl;

    // Create offscreen canvas texture for caching finished strokes (use render size!)
    s->canvas_texture = SDL_CreateTexture(s->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        s->render_width, s->render_height);
    if (!s->canvas_texture) {
        std::cerr << "[drawing_canvas] Failed to create canvas texture: " << SDL_GetError() << std::endl;
        // Non-fatal, we can still render without caching
    } else {
        SDL_SetTextureBlendMode(s->canvas_texture, SDL_BLENDMODE_BLEND);
        s->canvas_dirty = true;
    }

    // Reserve space for vertices
    s->vertices.reserve(MAX_VERTICES);

    s->initialized = true;
    std::cout << "[drawing_canvas] Initialized " << width << "x" << height << std::endl;
    return true;
}

// =============================================================================
// Cleanup
// =============================================================================

inline void cleanup() {
    auto* s = get_state();
    if (!s) return;

    if (s->canvas_texture) {
        SDL_DestroyTexture(s->canvas_texture);
    }
    if (s->renderer) {
        SDL_DestroyRenderer(s->renderer);
    }
    if (s->window) {
        SDL_DestroyWindow(s->window);
    }
    SDL_Quit();

    delete s;
    *get_state_ptr() = nullptr;
    std::cout << "[drawing_canvas] Cleaned up" << std::endl;
}

// =============================================================================
// Window state
// =============================================================================

inline bool should_close() {
    auto* s = get_state();
    return s ? !s->running : true;
}

inline int get_width() {
    auto* s = get_state();
    return s ? s->render_width : 0;  // Return render size for consistent coordinates
}

inline int get_height() {
    auto* s = get_state();
    return s ? s->render_height : 0;  // Return render size for consistent coordinates
}

// =============================================================================
// Event polling
// =============================================================================

inline int poll_events() {
    auto* s = get_state();
    if (!s) return 0;

    s->eventCount = 0;
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                s->running = false;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                s->running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                s->width = event.window.data1;
                s->height = event.window.data2;
                // Also update render size for high-DPI
                SDL_GetRenderOutputSize(s->renderer, &s->render_width, &s->render_height);
                break;

            // Mouse events
            // SDL3 mouse coords are in POINTS (window coords), need to convert to render pixels
            // Scale factor = render_size / window_size
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT && s->eventCount < MAX_EVENTS) {
                    float scale_x = (float)s->render_width / (float)s->width;
                    float scale_y = (float)s->render_height / (float)s->height;
                    s->events[s->eventCount++] = {
                        0,  // down
                        event.button.x * scale_x,
                        event.button.y * scale_y,
                        1.0f,  // Full pressure for mouse
                        false
                    };
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if ((event.motion.state & SDL_BUTTON_LMASK) && s->eventCount < MAX_EVENTS) {
                    float scale_x = (float)s->render_width / (float)s->width;
                    float scale_y = (float)s->render_height / (float)s->height;
                    s->events[s->eventCount++] = {
                        1,  // move
                        event.motion.x * scale_x,
                        event.motion.y * scale_y,
                        1.0f,
                        false
                    };
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && s->eventCount < MAX_EVENTS) {
                    float scale_x = (float)s->render_width / (float)s->width;
                    float scale_y = (float)s->render_height / (float)s->height;
                    s->events[s->eventCount++] = {
                        2,  // up
                        event.button.x * scale_x,
                        event.button.y * scale_y,
                        0.0f,
                        false
                    };
                }
                break;

            // Touch events (for iOS/iPad)
            // SDL3 touch coords are NORMALIZED (0-1) - multiply by render size
            case SDL_EVENT_FINGER_DOWN:
                {
                    // Touch coords are normalized 0-1, convert to render pixels
                    float x = event.tfinger.x * s->render_width;
                    float y = event.tfinger.y * s->render_height;

                    TOUCH_LOG("DOWN raw=(%.3f,%.3f) -> (%.1f,%.1f) render=(%d,%d)",
                              event.tfinger.x, event.tfinger.y, x, y,
                              s->render_width, s->render_height);

                    if (s->eventCount < MAX_EVENTS) {
                        s->events[s->eventCount++] = {
                            0,  // down
                            x,
                            y,
                            event.tfinger.pressure,
                            false
                        };
                    }
                }
                break;

            case SDL_EVENT_FINGER_MOTION:
                {
                    float x = event.tfinger.x * s->render_width;
                    float y = event.tfinger.y * s->render_height;

                    if (s->eventCount < MAX_EVENTS) {
                        s->events[s->eventCount++] = {
                            1,  // move
                            x,
                            y,
                            event.tfinger.pressure,
                            false
                        };
                    }
                }
                break;

            case SDL_EVENT_FINGER_UP:
                {
                    float x = event.tfinger.x * s->render_width;
                    float y = event.tfinger.y * s->render_height;

                    TOUCH_LOG("UP raw=(%.3f,%.3f) -> (%.1f,%.1f)",
                              event.tfinger.x, event.tfinger.y, x, y);

                    if (s->eventCount < MAX_EVENTS) {
                        s->events[s->eventCount++] = {
                            2,  // up
                            x,
                            y,
                            0.0f,
                            false
                        };
                    }
                }
                break;
        }
    }

    return s->eventCount;
}

// =============================================================================
// Event accessors (called from jank)
// =============================================================================

inline int get_event_count() {
    auto* s = get_state();
    return s ? s->eventCount : 0;
}

inline int get_event_type(int idx) {
    auto* s = get_state();
    if (!s || idx < 0 || idx >= s->eventCount) return -1;
    return s->events[idx].type;
}

inline float get_event_x(int idx) {
    auto* s = get_state();
    if (!s || idx < 0 || idx >= s->eventCount) return 0;
    return s->events[idx].x;
}

inline float get_event_y(int idx) {
    auto* s = get_state();
    if (!s || idx < 0 || idx >= s->eventCount) return 0;
    return s->events[idx].y;
}

inline float get_event_pressure(int idx) {
    auto* s = get_state();
    if (!s || idx < 0 || idx >= s->eventCount) return 1.0f;
    return s->events[idx].pressure;
}

inline bool get_event_is_pencil(int idx) {
    auto* s = get_state();
    if (!s || idx < 0 || idx >= s->eventCount) return false;
    return s->events[idx].is_pencil;
}

inline int64_t get_time_ms() {
    return SDL_GetTicks();
}

// =============================================================================
// Vertex buffer management
// =============================================================================

inline void clear_vertices() {
    auto* s = get_state();
    if (s) {
        s->vertices.clear();
    }
}

inline void add_triangle(float x1, float y1, float x2, float y2, float x3, float y3,
                         float r, float g, float b, float a) {
    auto* s = get_state();
    if (!s || s->vertices.size() + 3 > MAX_VERTICES) return;

    s->vertices.push_back({x1, y1, r, g, b, a});
    s->vertices.push_back({x2, y2, r, g, b, a});
    s->vertices.push_back({x3, y3, r, g, b, a});
}

inline void set_bg_color(float r, float g, float b, float a) {
    auto* s = get_state();
    if (s) {
        s->bgColor[0] = r;
        s->bgColor[1] = g;
        s->bgColor[2] = b;
        s->bgColor[3] = a;
    }
}

// =============================================================================
// Canvas Texture (for caching finished strokes)
// =============================================================================

// Clear the canvas texture to background color
inline void clear_canvas() {
    auto* s = get_state();
    if (!s || !s->canvas_texture) return;

    TOUCH_LOG("CLEAR_CANVAS called! canvas_dirty was %d", s->canvas_dirty ? 1 : 0);

    SDL_SetRenderTarget(s->renderer, s->canvas_texture);
    SDL_SetRenderDrawColor(s->renderer,
                           (Uint8)(s->bgColor[0] * 255),
                           (Uint8)(s->bgColor[1] * 255),
                           (Uint8)(s->bgColor[2] * 255),
                           (Uint8)(s->bgColor[3] * 255));
    SDL_RenderClear(s->renderer);
    SDL_SetRenderTarget(s->renderer, nullptr);
    s->canvas_dirty = false;
}

// Start rendering to canvas texture (for finished strokes)
inline void begin_canvas_render() {
    auto* s = get_state();
    if (!s || !s->canvas_texture) return;

    // Clear on first use
    if (s->canvas_dirty) {
        TOUCH_LOG("begin_canvas_render: canvas_dirty=true, clearing!");
        clear_canvas();
    }

    SDL_SetRenderTarget(s->renderer, s->canvas_texture);
}

// Stop rendering to canvas texture
inline void end_canvas_render() {
    auto* s = get_state();
    if (!s) return;

    SDL_SetRenderTarget(s->renderer, nullptr);
}

// Render vertices to current target (screen or texture)
inline void render_vertices() {
    auto* s = get_state();
    if (!s || !s->renderer || s->vertices.empty()) return;

    // Convert our vertices to SDL_Vertex format
    std::vector<SDL_Vertex> sdl_verts;
    sdl_verts.reserve(s->vertices.size());

    for (const auto& v : s->vertices) {
        SDL_Vertex sv;
        sv.position.x = v.x;
        sv.position.y = v.y;
        sv.color.r = v.r;
        sv.color.g = v.g;
        sv.color.b = v.b;
        sv.color.a = v.a;
        sv.tex_coord.x = 0;
        sv.tex_coord.y = 0;
        sdl_verts.push_back(sv);
    }

    // Render triangles
    SDL_RenderGeometry(s->renderer, nullptr,
                      sdl_verts.data(), (int)sdl_verts.size(),
                      nullptr, 0);
}

// Draw the cached canvas texture to screen
inline void draw_canvas() {
    auto* s = get_state();
    if (!s || !s->canvas_texture) return;

    // Clear canvas on first use if needed
    if (s->canvas_dirty) {
        clear_canvas();
    }

    SDL_RenderTexture(s->renderer, s->canvas_texture, nullptr, nullptr);
}

// Check if canvas texture is available
inline bool has_canvas_texture() {
    auto* s = get_state();
    return s && s->canvas_texture != nullptr;
}

// =============================================================================
// Rendering
// =============================================================================

inline void begin_frame() {
    auto* s = get_state();
    if (!s || !s->renderer) return;

    // Clear with background color
    SDL_SetRenderDrawColor(s->renderer,
                           (Uint8)(s->bgColor[0] * 255),
                           (Uint8)(s->bgColor[1] * 255),
                           (Uint8)(s->bgColor[2] * 255),
                           (Uint8)(s->bgColor[3] * 255));
    SDL_RenderClear(s->renderer);
}

inline void end_frame() {
    auto* s = get_state();
    if (!s || !s->renderer) return;

    // Render all triangles
    // SDL_Renderer doesn't have native triangle support, so we use SDL_RenderGeometry
    // which takes SDL_Vertex array

    if (!s->vertices.empty()) {
        // Convert our vertices to SDL_Vertex format
        std::vector<SDL_Vertex> sdl_verts;
        sdl_verts.reserve(s->vertices.size());

        for (const auto& v : s->vertices) {
            SDL_Vertex sv;
            sv.position.x = v.x;
            sv.position.y = v.y;
            sv.color.r = v.r;
            sv.color.g = v.g;
            sv.color.b = v.b;
            sv.color.a = v.a;
            sv.tex_coord.x = 0;
            sv.tex_coord.y = 0;
            sdl_verts.push_back(sv);
        }

        // Render triangles (no indices, every 3 vertices = 1 triangle)
        SDL_RenderGeometry(s->renderer, nullptr,
                          sdl_verts.data(), (int)sdl_verts.size(),
                          nullptr, 0);
    }

    // Present
    SDL_RenderPresent(s->renderer);
}

// =============================================================================
// Screenshot
// =============================================================================

inline bool take_screenshot(const char* filename) {
    auto* s = get_state();
    if (!s || !s->renderer) return false;

    // SDL3: SDL_RenderReadPixels returns an SDL_Surface* directly
    SDL_Surface* surface = SDL_RenderReadPixels(s->renderer, nullptr);
    if (!surface) {
        std::cerr << "[drawing_canvas] Failed to read pixels: " << SDL_GetError() << std::endl;
        return false;
    }

    // Save as BMP (SDL3 built-in)
    bool success = SDL_SaveBMP(surface, filename);

    if (!success) {
        std::cerr << "[drawing_canvas] Failed to save screenshot: " << SDL_GetError() << std::endl;
    } else {
        std::cout << "[drawing_canvas] Screenshot saved: " << filename << std::endl;
    }

    SDL_DestroySurface(surface);
    return success;
}

// Get current timestamp string for filenames
inline int64_t get_timestamp() {
    return SDL_GetTicks();
}

// =============================================================================
// Frame Timing (for performance measurement)
// =============================================================================

inline void start_frame_timing() {
    auto* s = get_state();
    if (!s) return;
    s->frame_start_time = SDL_GetPerformanceCounter();
}

inline void end_frame_timing() {
    auto* s = get_state();
    if (!s) return;

    uint64_t end_time = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    s->last_frame_time = end_time - s->frame_start_time;

    // Calculate frame time in milliseconds
    double frame_ms = (double)s->last_frame_time * 1000.0 / (double)freq;

    // Update rolling average (exponential moving average)
    if (s->frame_count == 0) {
        s->avg_frame_time = frame_ms;
    } else {
        s->avg_frame_time = s->avg_frame_time * 0.95 + frame_ms * 0.05;
    }
    s->frame_count++;
}

inline double get_last_frame_ms() {
    auto* s = get_state();
    if (!s) return 0.0;
    uint64_t freq = SDL_GetPerformanceFrequency();
    return (double)s->last_frame_time * 1000.0 / (double)freq;
}

inline double get_avg_frame_ms() {
    auto* s = get_state();
    return s ? s->avg_frame_time : 0.0;
}

inline double get_fps() {
    auto* s = get_state();
    if (!s || s->avg_frame_time <= 0.0) return 0.0;
    return 1000.0 / s->avg_frame_time;
}

inline int get_vertex_count() {
    auto* s = get_state();
    return s ? (int)s->vertices.size() : 0;
}

// =============================================================================
// Metal Stamp Renderer (GPU-accelerated drawing)
// =============================================================================

// =============================================================================
// Metal Stamp Renderer - use extern "C" functions for JIT compatibility
// =============================================================================

// Simple test function to verify linking
inline int test_metal_add(int a, int b) {
    return metal_test_add(a, b);
}

// Initialize Metal stamp renderer
inline bool init_metal_renderer() {
    auto* s = get_state();
    if (!s || !s->window) return false;

    bool success = metal_stamp_init(s->window, s->render_width, s->render_height);
    if (success) {
        s->use_metal_renderer = true;
        std::cout << "[drawing_canvas] Metal stamp renderer initialized" << std::endl;
    } else {
        std::cout << "[drawing_canvas] Metal renderer init failed" << std::endl;
    }
    return success;
}

// Cleanup Metal renderer
inline void cleanup_metal_renderer() {
    metal_stamp_cleanup();
    auto* s = get_state();
    if (s) s->use_metal_renderer = false;
}

// Check if Metal rendering is active
inline bool is_using_metal() {
    auto* s = get_state();
    return s && s->use_metal_renderer && metal_stamp_is_available();
}

// =============================================================================
// Metal Brush Settings (wrapper functions using extern "C")
// =============================================================================

inline void metal_set_brush_size(float size) {
    metal_stamp_set_brush_size(size);
}

inline void metal_set_brush_hardness(float hardness) {
    metal_stamp_set_brush_hardness(hardness);
}

inline void metal_set_brush_opacity(float opacity) {
    metal_stamp_set_brush_opacity(opacity);
}

inline void metal_set_brush_spacing(float spacing) {
    metal_stamp_set_brush_spacing(spacing);
}

inline void metal_set_brush_color(float r, float g, float b, float a) {
    metal_stamp_set_brush_color(r, g, b, a);
}

// =============================================================================
// Metal Stroke Functions (wrapper functions using extern "C")
// =============================================================================

inline void metal_begin_stroke(float x, float y, float pressure) {
    metal_stamp_begin_stroke(x, y, pressure);
}

inline void metal_add_stroke_point(float x, float y, float pressure) {
    metal_stamp_add_stroke_point(x, y, pressure);
}

inline void metal_end_stroke() {
    metal_stamp_end_stroke();
}

inline void metal_cancel_stroke() {
    metal_stamp_cancel_stroke();
}

// =============================================================================
// Metal Canvas Functions (wrapper functions using extern "C")
// =============================================================================

inline void metal_clear_canvas(float r, float g, float b, float a) {
    metal_stamp_clear_canvas(r, g, b, a);
}

inline void metal_render_stroke() {
    metal_stamp_render_stroke();
}

inline void metal_present() {
    metal_stamp_present();
}

} // namespace dc
