// Metal Stamp Renderer - GPU-accelerated brush stroke rendering
// Implements Procreate-style stamp-based rendering for smooth strokes
//
// This module provides a high-performance GPU rendering pipeline for brush
// strokes, using Metal on iOS/macOS. It replaces CPU-based tessellation with
// GPU point sprite rendering for smooth, artifact-free brush strokes.

#pragma once

#include <cstdint>

// Forward declarations for Objective-C types
#ifdef __OBJC__
@class MetalStampRendererImpl;
#else
typedef void MetalStampRendererImpl;
#endif

namespace metal_stamp {

// =============================================================================
// Constants
// =============================================================================

constexpr int MAX_POINTS_PER_STROKE = 10000;
constexpr float DEFAULT_SPACING = 0.15f;      // Spacing as fraction of brush size
constexpr float DEFAULT_HARDNESS = 0.0f;      // 0.0 = soft, 1.0 = hard edge
constexpr float DEFAULT_OPACITY = 1.0f;

// =============================================================================
// Point Data (matches MSL Point struct)
// =============================================================================

struct Point {
    float x, y;       // Position in screen coordinates (pixels)
    float size;       // Point size (diameter in pixels)
    float r, g, b, a; // Color
};

// =============================================================================
// Brush Settings
// =============================================================================

struct BrushSettings {
    float size = 20.0f;        // Brush diameter in pixels
    float hardness = 0.0f;     // Edge hardness (0=soft, 1=hard)
    float opacity = 1.0f;      // Stroke opacity
    float spacing = 0.15f;     // Spacing between stamps (fraction of size)
    float r = 0.0f;            // Color
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// =============================================================================
// MetalStampRenderer - C++ interface to Metal rendering
// =============================================================================

class MetalStampRenderer {
public:
    MetalStampRenderer();
    ~MetalStampRenderer();

    // Initialization - must be called before use
    // Pass the SDL_Window pointer to extract the CAMetalLayer
    bool init(void* sdl_window, int width, int height);

    // Check if initialized and ready
    bool is_ready() const;

    // Cleanup resources
    void cleanup();

    // =========================================================================
    // Brush Settings
    // =========================================================================

    void set_brush_size(float size);
    void set_brush_hardness(float hardness);  // 0.0 = soft, 1.0 = hard
    void set_brush_opacity(float opacity);
    void set_brush_spacing(float spacing);    // fraction of size
    void set_brush_color(float r, float g, float b, float a);
    void set_brush(const BrushSettings& settings);

    // Get current brush settings
    BrushSettings get_brush() const;

    // =========================================================================
    // Stroke Management
    // =========================================================================

    // Begin a new stroke at the given position
    void begin_stroke(float x, float y, float pressure = 1.0f);

    // Add a point to the current stroke (interpolates points for smoothness)
    void add_stroke_point(float x, float y, float pressure = 1.0f);

    // End the current stroke and commit to canvas texture
    void end_stroke();

    // Cancel current stroke without committing
    void cancel_stroke();

    // =========================================================================
    // Canvas Management
    // =========================================================================

    // Clear the canvas to background color
    void clear_canvas(float r, float g, float b, float a);

    // Get the canvas texture as an SDL texture (for compositing)
    // Returns nullptr if not available
    void* get_canvas_sdl_texture();

    // Resize the canvas (e.g., on window resize)
    void resize(int width, int height);

    // =========================================================================
    // Rendering
    // =========================================================================

    // Render current stroke to Metal layer (call each frame while drawing)
    void render_current_stroke();

    // Composite the canvas texture onto the screen
    void present();

    // =========================================================================
    // Debug
    // =========================================================================

    // Get number of points in current stroke
    int get_current_stroke_point_count() const;

    // Get total points rendered this frame
    int get_points_rendered() const;

private:
    // Prevent copying
    MetalStampRenderer(const MetalStampRenderer&) = delete;
    MetalStampRenderer& operator=(const MetalStampRenderer&) = delete;

    // Objective-C++ implementation (PIMPL pattern)
    MetalStampRendererImpl* impl_;

    // Current brush settings
    BrushSettings brush_;

    // State
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
};

} // namespace metal_stamp

// =============================================================================
// Global convenience functions (for jank JIT integration)
// Use extern "C" for predictable symbol names that JIT can find
// =============================================================================

extern "C" {

// Initialize the global Metal renderer
bool metal_stamp_init(void* sdl_window, int width, int height);

// Cleanup global renderer
void metal_stamp_cleanup();

// Check if Metal rendering is available
bool metal_stamp_is_available();

// Brush functions
void metal_stamp_set_brush_size(float size);
void metal_stamp_set_brush_hardness(float hardness);
void metal_stamp_set_brush_opacity(float opacity);
void metal_stamp_set_brush_spacing(float spacing);
void metal_stamp_set_brush_color(float r, float g, float b, float a);

// Stroke functions
void metal_stamp_begin_stroke(float x, float y, float pressure);
void metal_stamp_add_stroke_point(float x, float y, float pressure);
void metal_stamp_end_stroke();
void metal_stamp_cancel_stroke();

// Canvas functions
void metal_stamp_clear_canvas(float r, float g, float b, float a);
void metal_stamp_render_stroke();
void metal_stamp_present();

} // extern "C"
