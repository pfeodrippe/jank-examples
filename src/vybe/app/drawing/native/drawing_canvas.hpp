// Drawing Canvas - Minimal native layer for 2D stroke rendering
// RULE: Only put here what CANNOT be done in jank!
// - GPU triangle submission (via SDL_Renderer)
// - SDL window/event handling
// - Touch pressure capture
//
// Everything else (bezier math, tessellation, state) is in jank!
//
// NOTE: Using SDL_Renderer for simplicity - uses Metal on macOS/iOS automatically

#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <iostream>

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
    int width = 1024;
    int height = 768;
    bool running = true;

    // Vertex buffer (CPU-side, we'll render triangles via SDL)
    std::vector<Vertex2D> vertices;

    // Input events for current frame
    InputEvent events[MAX_EVENTS];
    int eventCount = 0;

    // Background color
    float bgColor[4] = {0.95f, 0.95f, 0.92f, 1.0f};  // Off-white paper color

    bool initialized = false;
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
    return s ? s->width : 0;
}

inline int get_height() {
    auto* s = get_state();
    return s ? s->height : 0;
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
                break;

            // Mouse events
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT && s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        0,  // down
                        event.button.x,
                        event.button.y,
                        1.0f,  // Full pressure for mouse
                        false
                    };
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if ((event.motion.state & SDL_BUTTON_LMASK) && s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        1,  // move
                        event.motion.x,
                        event.motion.y,
                        1.0f,
                        false
                    };
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        2,  // up
                        event.button.x,
                        event.button.y,
                        0.0f,
                        false
                    };
                }
                break;

            // Touch events (for iOS/iPad)
            case SDL_EVENT_FINGER_DOWN:
                if (s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        0,
                        event.tfinger.x * s->width,
                        event.tfinger.y * s->height,
                        event.tfinger.pressure,
                        false  // Could detect Apple Pencil here
                    };
                }
                break;

            case SDL_EVENT_FINGER_MOTION:
                if (s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        1,
                        event.tfinger.x * s->width,
                        event.tfinger.y * s->height,
                        event.tfinger.pressure,
                        false
                    };
                }
                break;

            case SDL_EVENT_FINGER_UP:
                if (s->eventCount < MAX_EVENTS) {
                    s->events[s->eventCount++] = {
                        2,
                        event.tfinger.x * s->width,
                        event.tfinger.y * s->height,
                        0.0f,
                        false
                    };
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

} // namespace dc
