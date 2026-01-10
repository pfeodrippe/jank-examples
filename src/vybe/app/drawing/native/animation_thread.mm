// animation_thread.mm - Implementation of Looom-style animation threads
// Stores strokes per frame for memory-efficient animation

#include "animation_thread.h"
#include "metal_renderer.h"
#include <cstdlib>
#include <ctime>

namespace animation {

// =============================================================================
// Global Weave Singleton
// =============================================================================

static Weave* g_weave = nullptr;
static AnimStroke* g_currentStroke = nullptr;
static float g_strokeStartTime = 0.0f;
static FrameChangeCallback g_frameChangeCallback = nullptr;

Weave& getCurrentWeave() {
    if (!g_weave) {
        g_weave = new Weave();
        // Initialize with one thread
        g_weave->addThread();
    }
    return *g_weave;
}

// =============================================================================
// Current brush settings (set before stroke begins)
// =============================================================================

static StrokeBrush g_currentBrush = {};
static float g_currentColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

} // namespace animation

// =============================================================================
// C API Implementation
// =============================================================================

extern "C" {

// ---------------------------------------------------------------------------
// Weave Management
// ---------------------------------------------------------------------------

void anim_weave_init() {
    if (animation::g_weave) {
        delete animation::g_weave;
    }
    animation::g_weave = new animation::Weave();
    animation::g_weave->addThread();
    animation::g_currentStroke = nullptr;

    // Seed random for random play mode
    srand((unsigned int)time(nullptr));
}

void anim_weave_cleanup() {
    if (animation::g_currentStroke) {
        delete animation::g_currentStroke;
        animation::g_currentStroke = nullptr;
    }
    if (animation::g_weave) {
        delete animation::g_weave;
        animation::g_weave = nullptr;
    }
}

void anim_weave_clear() {
    animation::Weave& weave = animation::getCurrentWeave();
    weave.threads.clear();
    weave.activeThreadIndex = 0;
    weave.addThread();
}

// ---------------------------------------------------------------------------
// Thread Management
// ---------------------------------------------------------------------------

int32_t anim_add_thread() {
    animation::Weave& weave = animation::getCurrentWeave();
    animation::AnimThread* thread = weave.addThread();
    if (thread) {
        return (int32_t)(weave.threads.size() - 1);
    }
    return -1;
}

void anim_delete_thread(int32_t index) {
    animation::getCurrentWeave().deleteThread(index);
}

void anim_select_thread(int32_t index) {
    animation::getCurrentWeave().selectThread(index);
}

int32_t anim_get_active_thread_index() {
    return animation::getCurrentWeave().activeThreadIndex;
}

int32_t anim_get_thread_count() {
    return animation::getCurrentWeave().threadCount();
}

// ---------------------------------------------------------------------------
// Frame Management
// ---------------------------------------------------------------------------

void anim_add_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->addFrame();
    }
}

void anim_add_frame_after_current() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->addFrameAt(thread->currentFrameIndex + 1);
        thread->currentFrameIndex++;
    }
}

void anim_delete_current_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->deleteCurrentFrame();
    }
}

void anim_duplicate_current_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->duplicateCurrentFrame();
    }
}

void anim_clear_current_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        animation::AnimFrame* frame = thread->getCurrentFrame();
        if (frame) {
            frame->clear();
        }
    }
}

void anim_next_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->nextFrame();
        if (animation::g_frameChangeCallback) {
            animation::g_frameChangeCallback(thread->currentFrameIndex);
        }
    }
}

void anim_prev_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->prevFrame();
        if (animation::g_frameChangeCallback) {
            animation::g_frameChangeCallback(thread->currentFrameIndex);
        }
    }
}

void anim_goto_frame(int32_t index) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->goToFrame(index);
        if (animation::g_frameChangeCallback) {
            animation::g_frameChangeCallback(thread->currentFrameIndex);
        }
    }
}

void anim_set_frame_change_callback(FrameChangeCallback callback) {
    animation::g_frameChangeCallback = callback;
}

int32_t anim_get_current_frame_index() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        return thread->currentFrameIndex;
    }
    return -1;
}

int32_t anim_get_frame_count() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        return thread->frameCount();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Thread Properties
// ---------------------------------------------------------------------------

void anim_set_thread_fps(float fps) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->fps = std::max(animation::MIN_FPS, std::min(fps, animation::MAX_FPS));
    }
}

float anim_get_thread_fps() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        return thread->fps;
    }
    return animation::DEFAULT_FPS;
}

void anim_set_thread_play_mode(int32_t mode) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread && mode >= 0 && mode <= 3) {
        thread->playMode = static_cast<animation::PlayMode>(mode);
    }
}

int32_t anim_get_thread_play_mode() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        return static_cast<int32_t>(thread->playMode);
    }
    return 0;
}

void anim_set_thread_visible(bool visible) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->visible = visible;
    }
}

bool anim_get_thread_visible() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        return thread->visible;
    }
    return false;
}

void anim_set_thread_color(float r, float g, float b) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        thread->style.r = r;
        thread->style.g = g;
        thread->style.b = b;
    }
}

// ---------------------------------------------------------------------------
// Playback Control
// ---------------------------------------------------------------------------

void anim_play() {
    animation::getCurrentWeave().play();
}

void anim_pause() {
    animation::getCurrentWeave().pause();
}

void anim_toggle_playback() {
    animation::getCurrentWeave().togglePlayback();
}

void anim_stop() {
    animation::getCurrentWeave().stop();
}

bool anim_is_playing() {
    return animation::getCurrentWeave().isPlaying;
}

void anim_set_global_speed(float speed) {
    animation::getCurrentWeave().globalSpeed = std::max(0.1f, std::min(speed, 10.0f));
}

float anim_get_global_speed() {
    return animation::getCurrentWeave().globalSpeed;
}

void anim_update(float deltaTime) {
    animation::getCurrentWeave().update(deltaTime);
}

// ---------------------------------------------------------------------------
// Onion Skin
// ---------------------------------------------------------------------------

void anim_set_onion_skin_mode(int32_t mode) {
    animation::Weave& weave = animation::getCurrentWeave();
    if (mode >= 0 && mode <= 3) {
        weave.onionSkinMode = static_cast<animation::OnionSkinMode>(mode);
    }
}

int32_t anim_get_onion_skin_mode() {
    return static_cast<int32_t>(animation::getCurrentWeave().onionSkinMode);
}

void anim_set_onion_skin_frames(int32_t before, int32_t after) {
    animation::Weave& weave = animation::getCurrentWeave();
    weave.onionSkinBefore = std::max(0, std::min(before, 10));
    weave.onionSkinAfter = std::max(0, std::min(after, 10));
}

void anim_set_onion_skin_opacity(float opacity) {
    animation::getCurrentWeave().onionSkinOpacity = std::max(0.0f, std::min(opacity, 1.0f));
}

// ---------------------------------------------------------------------------
// Stroke Recording
// ---------------------------------------------------------------------------

// Set brush settings before beginning stroke (call from jank before anim_begin_stroke)
void anim_set_brush(float size, float hardness, float opacity, float spacing,
                    int32_t shapeTexId, int32_t grainTexId, float grainScale, int shapeInverted,
                    float sizePressure, float opacityPressure,
                    float sizeJitter, float opacityJitter, float rotationJitter, float scatter) {
    animation::g_currentBrush.brushType = 0;
    animation::g_currentBrush.size = size;
    animation::g_currentBrush.hardness = hardness;
    animation::g_currentBrush.opacity = opacity;
    animation::g_currentBrush.spacing = spacing;
    animation::g_currentBrush.shapeTextureId = shapeTexId;
    animation::g_currentBrush.grainTextureId = grainTexId;
    animation::g_currentBrush.grainScale = grainScale;
    animation::g_currentBrush.shapeInverted = shapeInverted;
    animation::g_currentBrush.sizePressure = sizePressure;
    animation::g_currentBrush.opacityPressure = opacityPressure;
    animation::g_currentBrush.sizeJitter = sizeJitter;
    animation::g_currentBrush.opacityJitter = opacityJitter;
    animation::g_currentBrush.rotationJitter = rotationJitter;
    animation::g_currentBrush.scatter = scatter;
}

// Set color before beginning stroke
void anim_set_stroke_color(float r, float g, float b, float a) {
    animation::g_currentColor[0] = r;
    animation::g_currentColor[1] = g;
    animation::g_currentColor[2] = b;
    animation::g_currentColor[3] = a;
}

void anim_begin_stroke(float x, float y, float pressure) {
    // Create new stroke
    if (animation::g_currentStroke) {
        delete animation::g_currentStroke;
    }
    animation::g_currentStroke = new animation::AnimStroke();

    // Copy current brush settings
    animation::g_currentStroke->brush = animation::g_currentBrush;

    // Use stored color
    animation::g_currentStroke->r = animation::g_currentColor[0];
    animation::g_currentStroke->g = animation::g_currentColor[1];
    animation::g_currentStroke->b = animation::g_currentColor[2];
    animation::g_currentStroke->a = animation::g_currentColor[3];

    // Add first point
    animation::StrokePoint point;
    point.x = x;
    point.y = y;
    point.pressure = pressure;
    point.timestamp = 0.0f;
    animation::g_currentStroke->points.push_back(point);

    animation::g_strokeStartTime = 0.0f;
}

void anim_add_stroke_point(float x, float y, float pressure) {
    if (!animation::g_currentStroke) return;

    animation::StrokePoint point;
    point.x = x;
    point.y = y;
    point.pressure = pressure;
    point.timestamp = 0.0f;  // Would use real elapsed time
    animation::g_currentStroke->points.push_back(point);
}

void anim_end_stroke() {
    if (!animation::g_currentStroke) return;

    // Update bounding box
    animation::g_currentStroke->updateBounds();

    // Add stroke to current frame of active thread
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        animation::AnimFrame* frame = thread->getCurrentFrame();
        if (frame) {
            frame->strokes.push_back(std::move(*animation::g_currentStroke));
            frame->invalidateCache();
        }
    }

    delete animation::g_currentStroke;
    animation::g_currentStroke = nullptr;
}

void anim_cancel_stroke() {
    if (animation::g_currentStroke) {
        delete animation::g_currentStroke;
        animation::g_currentStroke = nullptr;
    }
}

int32_t anim_get_current_frame_stroke_count() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (thread) {
        animation::AnimFrame* frame = thread->getCurrentFrame();
        if (frame) {
            return (int32_t)frame->strokes.size();
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Frame Rendering - replay strokes to Metal renderer
// ---------------------------------------------------------------------------

// Render a single stroke to Metal (internal helper)
static void renderStrokeToMetal(const animation::AnimStroke& stroke) {
    if (stroke.points.empty()) return;

    // Set up brush settings
    metal_stamp_set_brush_size(stroke.brush.size);
    metal_stamp_set_brush_hardness(stroke.brush.hardness);
    metal_stamp_set_brush_opacity(stroke.brush.opacity);
    metal_stamp_set_brush_spacing(stroke.brush.spacing);
    metal_stamp_set_brush_shape_texture(stroke.brush.shapeTextureId);
    metal_stamp_set_brush_grain_texture(stroke.brush.grainTextureId);
    metal_stamp_set_brush_grain_scale(stroke.brush.grainScale);
    metal_stamp_set_brush_shape_inverted(stroke.brush.shapeInverted);
    metal_stamp_set_brush_size_pressure(stroke.brush.sizePressure);
    metal_stamp_set_brush_opacity_pressure(stroke.brush.opacityPressure);
    metal_stamp_set_brush_size_jitter(stroke.brush.sizeJitter);
    metal_stamp_set_brush_opacity_jitter(stroke.brush.opacityJitter);
    metal_stamp_set_brush_rotation_jitter(stroke.brush.rotationJitter);
    metal_stamp_set_brush_scatter(stroke.brush.scatter);

    // Set color
    metal_stamp_set_brush_color(stroke.r, stroke.g, stroke.b, stroke.a);

    // Begin stroke with first point
    const auto& firstPt = stroke.points[0];
    metal_stamp_begin_stroke(firstPt.x, firstPt.y, firstPt.pressure);

    // Add remaining points
    for (size_t i = 1; i < stroke.points.size(); i++) {
        const auto& pt = stroke.points[i];
        metal_stamp_add_stroke_point(pt.x, pt.y, pt.pressure);
    }

    // End stroke (commits to canvas)
    metal_stamp_end_stroke();
}

// Render current frame's strokes to Metal canvas
void anim_render_current_frame() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (!thread) return;

    animation::AnimFrame* frame = thread->getCurrentFrame();
    if (!frame) return;

    // Render each stroke
    for (const auto& stroke : frame->strokes) {
        renderStrokeToMetal(stroke);
    }
}

// Render a specific frame (by index) to Metal canvas
void anim_render_frame(int32_t frameIndex) {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (!thread) return;

    if (frameIndex < 0 || frameIndex >= (int32_t)thread->frames.size()) return;

    animation::AnimFrame& frame = thread->frames[frameIndex];

    // Render each stroke
    for (const auto& stroke : frame.strokes) {
        renderStrokeToMetal(stroke);
    }
}

// Render onion skin frames (ghost frames before/after current)
void anim_render_onion_skin() {
    animation::Weave& weave = animation::getCurrentWeave();
    if (weave.onionSkinMode == animation::OnionSkinMode::Off) return;

    animation::AnimThread* thread = weave.getActiveThread();
    if (!thread) return;

    int currentIdx = thread->currentFrameIndex;
    int frameCount = thread->frameCount();
    float baseOpacity = weave.onionSkinOpacity;

    // Render frames before current (red tint)
    if (weave.onionSkinMode == animation::OnionSkinMode::Before ||
        weave.onionSkinMode == animation::OnionSkinMode::Both) {
        for (int i = 1; i <= weave.onionSkinBefore; i++) {
            int idx = currentIdx - i;
            if (idx < 0) idx += frameCount;  // Wrap around
            if (idx >= 0 && idx < frameCount && idx != currentIdx) {
                float opacity = baseOpacity * (1.0f - (float)i / (weave.onionSkinBefore + 1));
                animation::AnimFrame& frame = thread->frames[idx];
                for (const auto& stroke : frame.strokes) {
                    // Render with reduced opacity and red tint
                    metal_stamp_set_brush_size(stroke.brush.size);
                    metal_stamp_set_brush_hardness(stroke.brush.hardness);
                    metal_stamp_set_brush_opacity(stroke.brush.opacity * opacity);
                    metal_stamp_set_brush_spacing(stroke.brush.spacing);
                    // Red tint for past frames
                    metal_stamp_set_brush_color(
                        stroke.r * 0.5f + 0.5f,  // More red
                        stroke.g * 0.3f,
                        stroke.b * 0.3f,
                        stroke.a * opacity
                    );
                    const auto& firstPt = stroke.points[0];
                    metal_stamp_begin_stroke(firstPt.x, firstPt.y, firstPt.pressure);
                    for (size_t j = 1; j < stroke.points.size(); j++) {
                        const auto& pt = stroke.points[j];
                        metal_stamp_add_stroke_point(pt.x, pt.y, pt.pressure);
                    }
                    metal_stamp_end_stroke();
                }
            }
        }
    }

    // Render frames after current (blue tint)
    if (weave.onionSkinMode == animation::OnionSkinMode::After ||
        weave.onionSkinMode == animation::OnionSkinMode::Both) {
        for (int i = 1; i <= weave.onionSkinAfter; i++) {
            int idx = (currentIdx + i) % frameCount;
            if (idx >= 0 && idx < frameCount && idx != currentIdx) {
                float opacity = baseOpacity * (1.0f - (float)i / (weave.onionSkinAfter + 1));
                animation::AnimFrame& frame = thread->frames[idx];
                for (const auto& stroke : frame.strokes) {
                    // Render with reduced opacity and blue tint
                    metal_stamp_set_brush_size(stroke.brush.size);
                    metal_stamp_set_brush_hardness(stroke.brush.hardness);
                    metal_stamp_set_brush_opacity(stroke.brush.opacity * opacity);
                    metal_stamp_set_brush_spacing(stroke.brush.spacing);
                    // Blue tint for future frames
                    metal_stamp_set_brush_color(
                        stroke.r * 0.3f,
                        stroke.g * 0.3f,
                        stroke.b * 0.5f + 0.5f,  // More blue
                        stroke.a * opacity
                    );
                    const auto& firstPt = stroke.points[0];
                    metal_stamp_begin_stroke(firstPt.x, firstPt.y, firstPt.pressure);
                    for (size_t j = 1; j < stroke.points.size(); j++) {
                        const auto& pt = stroke.points[j];
                        metal_stamp_add_stroke_point(pt.x, pt.y, pt.pressure);
                    }
                    metal_stamp_end_stroke();
                }
            }
        }
    }
}

// Get previous frame index for playback (returns -1 if frame didn't change)
int32_t anim_get_prev_frame_for_playback() {
    animation::AnimThread* thread = animation::getCurrentWeave().getActiveThread();
    if (!thread) return -1;
    // Store previous frame index before update
    return thread->currentFrameIndex;
}

} // extern "C"
