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
// Brush Type (selects shader pipeline)
// =============================================================================

enum class BrushType : int32_t {
    Round = 0,       // Default soft/hard circle
    Crayon = 1,      // Crayon with procedural grain texture
    Watercolor = 2,  // Soft wet edges
    Marker = 3       // Hard edges with streaking
};

// =============================================================================
// Brush Settings
// =============================================================================

struct BrushSettings {
    BrushType type = BrushType::Crayon;  // Default to crayon brush!
    float size = 20.0f;        // Brush diameter in pixels
    float hardness = 0.0f;     // Edge hardness (0=soft, 1=hard)
    float opacity = 1.0f;      // Stroke opacity
    float spacing = 0.15f;     // Spacing between stamps (fraction of size)
    float flow = 1.0f;         // Paint flow rate
    float r = 0.0f;            // Color
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    // Texture settings
    int32_t shape_texture_id = 0;   // 0 = default procedural circle
    int32_t grain_texture_id = 0;   // 0 = no grain
    float grain_scale = 1.0f;       // Grain texture scale
    bool grain_moving = true;       // true = moving, false = texturized

    // Dynamics
    float rotation = 0.0f;          // Base rotation in degrees
    float rotation_jitter = 0.0f;   // Random rotation range
    float scatter = 0.0f;           // Random offset from path
    float size_pressure = 1.0f;     // Pressure affects size (0-1)
    float opacity_pressure = 0.0f;  // Pressure affects opacity (0-1)
    float size_velocity = 0.0f;     // Speed affects size (-1 to 1)
    float size_jitter = 0.0f;       // Random size variation (0-1)
    float opacity_jitter = 0.0f;    // Random opacity variation (0-1)
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

    void set_brush_type(BrushType type);
    void set_brush_size(float size);
    void set_brush_hardness(float hardness);  // 0.0 = soft, 1.0 = hard
    void set_brush_opacity(float opacity);
    void set_brush_spacing(float spacing);    // fraction of size
    void set_brush_color(float r, float g, float b, float a);
    void set_brush(const BrushSettings& settings);
    void set_brush_rotation(float degrees);
    void set_brush_rotation_jitter(float degrees);
    void set_brush_scatter(float scatter);
    void set_brush_size_pressure(float amount);
    void set_brush_opacity_pressure(float amount);
    void set_brush_size_jitter(float amount);
    void set_brush_opacity_jitter(float amount);

    // Get current brush settings
    BrushSettings get_brush() const;

    // =========================================================================
    // Texture Management
    // =========================================================================

    // Load a texture from file (PNG, grayscale recommended)
    // Returns texture ID (>0 on success, 0 on failure)
    int32_t load_texture(const char* path);

    // Load texture from raw pixel data (single channel grayscale)
    int32_t load_texture_from_data(const uint8_t* data, int width, int height);

    // Set the shape texture for the brush (0 = procedural circle)
    void set_brush_shape_texture(int32_t texture_id);

    // Set the grain texture for the brush (0 = no grain)
    void set_brush_grain_texture(int32_t texture_id);
    void set_brush_grain_scale(float scale);
    void set_brush_grain_moving(bool moving);

    // Unload a texture
    void unload_texture(int32_t texture_id);

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
    // UI Drawing
    // =========================================================================

    // Queue a rounded rectangle to be drawn as UI overlay
    // Called before present(), drawn on top of canvas
    void queue_ui_rect(float x, float y, float width, float height,
                       float r, float g, float b, float a,
                       float corner_radius);

    // =========================================================================
    // Canvas Transform (Pan/Zoom/Rotate)
    // =========================================================================

    // Set the canvas transform for rendering
    // panX, panY: translation in screen pixels
    // scale: zoom factor (1.0 = 100%)
    // rotation: rotation in radians
    // pivotX, pivotY: transform center point in screen pixels
    void set_canvas_transform(float panX, float panY, float scale,
                              float rotation, float pivotX, float pivotY);

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

// Simple test function to verify linking works
int metal_test_add(int a, int b);

// Initialize the global Metal renderer
bool metal_stamp_init(void* sdl_window, int width, int height);

// Cleanup global renderer
void metal_stamp_cleanup();

// Check if Metal rendering is available
bool metal_stamp_is_available();

// Brush functions
void metal_stamp_set_brush_type(int32_t type);  // 0=Round, 1=Crayon, 2=Watercolor, 3=Marker
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

// Extended brush settings
void metal_stamp_set_brush_rotation(float degrees);
void metal_stamp_set_brush_rotation_jitter(float degrees);
void metal_stamp_set_brush_scatter(float scatter);
void metal_stamp_set_brush_size_pressure(float amount);
void metal_stamp_set_brush_opacity_pressure(float amount);
void metal_stamp_set_brush_size_jitter(float amount);
void metal_stamp_set_brush_opacity_jitter(float amount);

// Texture management
int32_t metal_stamp_load_texture(const char* path);
int32_t metal_stamp_load_texture_data(const uint8_t* data, int width, int height);
void metal_stamp_set_brush_shape_texture(int32_t texture_id);
void metal_stamp_set_brush_grain_texture(int32_t texture_id);
void metal_stamp_set_brush_grain_scale(float scale);
void metal_stamp_set_brush_grain_moving(bool moving);
void metal_stamp_unload_texture(int32_t texture_id);

// Built-in brush presets
void metal_stamp_use_preset_round_soft();
void metal_stamp_use_preset_round_hard();
void metal_stamp_use_preset_square();
void metal_stamp_use_preset_splatter();

// UI Drawing
void metal_stamp_queue_ui_rect(float x, float y, float width, float height,
                               float r, float g, float b, float a,
                               float corner_radius);

// Canvas Transform (pan/zoom/rotate)
void metal_stamp_set_canvas_transform(float panX, float panY, float scale,
                                      float rotation, float pivotX, float pivotY);

} // extern "C"
