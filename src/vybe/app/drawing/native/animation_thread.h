// animation_thread.h - Looom-style independent timeline animation system
// Each thread is an independent animated layer with its own frames, FPS, and play mode
// Multiple threads combine into a "weave" for complex polyrhythmic animations

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

namespace animation {

// =============================================================================
// Constants
// =============================================================================

constexpr int MAX_THREADS = 5;
constexpr int MAX_FRAMES_PER_THREAD = 100;
constexpr int MAX_STROKES_PER_FRAME = 1000;
constexpr int MAX_POINTS_PER_STROKE = 5000;
constexpr float DEFAULT_FPS = 12.0f;
constexpr float MIN_FPS = 1.0f;
constexpr float MAX_FPS = 60.0f;

// =============================================================================
// Enums
// =============================================================================

enum class PlayMode : int32_t {
    Forward = 0,    // 0 -> 1 -> 2 -> 3 -> 0 -> ...
    Backward = 1,   // 3 -> 2 -> 1 -> 0 -> 3 -> ...
    PingPong = 2,   // 0 -> 1 -> 2 -> 3 -> 2 -> 1 -> 0 -> ...
    Random = 3      // Random frame each step
};

enum class BlendMode : int32_t {
    Alpha = 0,      // Normal alpha blending
    Additive = 1,   // Add colors (screen-like)
    Multiply = 2,   // Multiply colors (darken)
    Invert = 3      // Invert/difference
};

enum class OnionSkinMode : int32_t {
    Off = 0,
    Before = 1,     // Show past frames only
    After = 2,      // Show future frames only
    Both = 3        // Show both past and future
};

// =============================================================================
// Stroke Data
// =============================================================================

struct StrokePoint {
    float x, y;         // Position in canvas coordinates
    float pressure;     // 0.0 - 1.0
    float timestamp;    // Time since stroke start (for velocity)
};

struct StrokeBrush {
    int32_t brushType;      // 0=Round, 1=Crayon, etc.
    float size;             // Brush size at pressure=1.0
    float hardness;         // Edge hardness 0-1
    float opacity;          // Stroke opacity 0-1
    float spacing;          // Spacing as fraction of size
    int32_t shapeTextureId; // Shape texture ID (0=default)
    int32_t grainTextureId; // Grain texture ID (0=none)
    float grainScale;
    int shapeInverted;

    // Dynamics
    float sizePressure;     // Pressure -> size
    float opacityPressure;  // Pressure -> opacity
    float sizeJitter;
    float opacityJitter;
    float rotationJitter;
    float scatter;
};

struct AnimStroke {
    std::vector<StrokePoint> points;
    StrokeBrush brush;
    float r, g, b, a;       // Stroke color (captured at draw time)

    // Bounding box for quick culling
    float minX, minY, maxX, maxY;

    void updateBounds() {
        if (points.empty()) {
            minX = minY = maxX = maxY = 0;
            return;
        }
        minX = maxX = points[0].x;
        minY = maxY = points[0].y;
        for (const auto& p : points) {
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }
        // Expand by brush size
        float expand = brush.size;
        minX -= expand;
        minY -= expand;
        maxX += expand;
        maxY += expand;
    }
};

// =============================================================================
// Frame
// =============================================================================

struct AnimFrame {
    std::vector<AnimStroke> strokes;
    bool isBookmark = false;    // Reference frame marker for ping-pong

    // Render cache (managed externally via texture IDs)
    int32_t cacheTextureId = -1;
    bool cacheValid = false;

    void invalidateCache() {
        cacheValid = false;
    }

    bool isEmpty() const {
        return strokes.empty();
    }

    void clear() {
        strokes.clear();
        invalidateCache();
    }
};

// =============================================================================
// Thread Style
// =============================================================================

struct ThreadStyle {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;  // Thread color
    float lineWidth = 3.0f;
    float opacity = 1.0f;
    BlendMode blendMode = BlendMode::Alpha;
    bool isFillMode = false;        // false = line mode
    bool pressureSensitive = true;
    bool clipToBelow = false;       // Mask to thread below
};

// =============================================================================
// Thread (Independent Timeline)
// =============================================================================

struct AnimThread {
    std::string name;
    std::vector<AnimFrame> frames;
    ThreadStyle style;

    // Playback settings
    float fps = DEFAULT_FPS;
    PlayMode playMode = PlayMode::Forward;

    // Playback state
    int currentFrameIndex = 0;
    float frameAccumulator = 0.0f;  // For sub-frame timing
    int pingPongDirection = 1;      // +1 or -1 for ping-pong

    // Editing state
    bool visible = true;
    bool locked = false;
    bool selected = false;

    // Time offset (for desync from weave time)
    float timeOffset = 0.0f;

    // =========================================================================
    // Frame Management
    // =========================================================================

    void addFrame() {
        if (frames.size() < MAX_FRAMES_PER_THREAD) {
            frames.emplace_back();
        }
    }

    void addFrameAt(int index) {
        if (frames.size() < MAX_FRAMES_PER_THREAD && index >= 0 && index <= (int)frames.size()) {
            frames.insert(frames.begin() + index, AnimFrame());
        }
    }

    void deleteFrame(int index) {
        if (index >= 0 && index < (int)frames.size() && frames.size() > 1) {
            frames.erase(frames.begin() + index);
            if (currentFrameIndex >= (int)frames.size()) {
                currentFrameIndex = frames.size() - 1;
            }
        }
    }

    void deleteCurrentFrame() {
        deleteFrame(currentFrameIndex);
    }

    void duplicateFrame(int index) {
        if (index >= 0 && index < (int)frames.size() && frames.size() < MAX_FRAMES_PER_THREAD) {
            AnimFrame copy = frames[index];
            copy.cacheTextureId = -1;
            copy.cacheValid = false;
            frames.insert(frames.begin() + index + 1, std::move(copy));
        }
    }

    void duplicateCurrentFrame() {
        duplicateFrame(currentFrameIndex);
    }

    AnimFrame* getCurrentFrame() {
        if (currentFrameIndex >= 0 && currentFrameIndex < (int)frames.size()) {
            return &frames[currentFrameIndex];
        }
        return nullptr;
    }

    const AnimFrame* getCurrentFrame() const {
        if (currentFrameIndex >= 0 && currentFrameIndex < (int)frames.size()) {
            return &frames[currentFrameIndex];
        }
        return nullptr;
    }

    // =========================================================================
    // Navigation
    // =========================================================================

    void goToFrame(int index) {
        if (!frames.empty()) {
            currentFrameIndex = std::max(0, std::min(index, (int)frames.size() - 1));
        }
    }

    void nextFrame() {
        if (!frames.empty()) {
            currentFrameIndex = (currentFrameIndex + 1) % frames.size();
        }
    }

    void prevFrame() {
        if (!frames.empty()) {
            currentFrameIndex = (currentFrameIndex - 1 + frames.size()) % frames.size();
        }
    }

    // =========================================================================
    // Playback
    // =========================================================================

    void update(float deltaTime, float globalSpeed = 1.0f) {
        if (frames.empty() || frames.size() == 1) return;

        float effectiveFps = fps * globalSpeed;
        if (effectiveFps <= 0) return;

        float frameTime = 1.0f / effectiveFps;
        frameAccumulator += deltaTime;

        while (frameAccumulator >= frameTime) {
            frameAccumulator -= frameTime;
            advanceFrame();
        }
    }

    void advanceFrame() {
        if (frames.empty()) return;

        switch (playMode) {
            case PlayMode::Forward:
                currentFrameIndex = (currentFrameIndex + 1) % frames.size();
                break;

            case PlayMode::Backward:
                currentFrameIndex = (currentFrameIndex - 1 + frames.size()) % frames.size();
                break;

            case PlayMode::PingPong: {
                // Check for bookmark anchor
                int nextIndex = currentFrameIndex + pingPongDirection;

                // Reverse at ends or bookmarks
                if (nextIndex >= (int)frames.size()) {
                    pingPongDirection = -1;
                    nextIndex = frames.size() - 2;
                } else if (nextIndex < 0) {
                    pingPongDirection = 1;
                    nextIndex = 1;
                }

                // Clamp to valid range
                currentFrameIndex = std::max(0, std::min(nextIndex, (int)frames.size() - 1));
                break;
            }

            case PlayMode::Random:
                if (frames.size() > 1) {
                    int newIndex;
                    do {
                        newIndex = rand() % frames.size();
                    } while (newIndex == currentFrameIndex && frames.size() > 1);
                    currentFrameIndex = newIndex;
                }
                break;
        }
    }

    void resetPlayback() {
        currentFrameIndex = 0;
        frameAccumulator = 0.0f;
        pingPongDirection = 1;
    }

    // =========================================================================
    // Utility
    // =========================================================================

    int frameCount() const { return (int)frames.size(); }

    float getDuration() const {
        if (fps <= 0 || frames.empty()) return 0;
        return frames.size() / fps;
    }

    void invalidateAllCaches() {
        for (auto& frame : frames) {
            frame.invalidateCache();
        }
    }
};

// =============================================================================
// Weave (Complete Animation Project)
// =============================================================================

struct Weave {
    std::vector<AnimThread> threads;
    int activeThreadIndex = 0;

    // Global settings
    float backgroundColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // White default
    float globalSpeed = 1.0f;
    bool isPlaying = false;

    // Onion skin settings
    OnionSkinMode onionSkinMode = OnionSkinMode::Off;
    int onionSkinBefore = 2;
    int onionSkinAfter = 2;
    float onionSkinOpacity = 0.3f;

    // Grid
    bool showGrid = false;
    float gridSize = 50.0f;

    // Canvas size
    int canvasWidth = 1024;
    int canvasHeight = 1024;

    // =========================================================================
    // Thread Management
    // =========================================================================

    AnimThread* addThread() {
        if (threads.size() >= MAX_THREADS) return nullptr;

        AnimThread thread;
        thread.name = "Thread " + std::to_string(threads.size() + 1);
        thread.addFrame();  // Start with one empty frame

        // Assign a default color based on index
        static const float colors[][3] = {
            {0.2f, 0.2f, 0.8f},  // Blue
            {0.8f, 0.2f, 0.2f},  // Red
            {0.2f, 0.7f, 0.2f},  // Green
            {0.8f, 0.6f, 0.1f},  // Orange
            {0.6f, 0.2f, 0.6f},  // Purple
        };
        int colorIdx = threads.size() % 5;
        thread.style.r = colors[colorIdx][0];
        thread.style.g = colors[colorIdx][1];
        thread.style.b = colors[colorIdx][2];

        threads.push_back(std::move(thread));
        return &threads.back();
    }

    void deleteThread(int index) {
        if (index >= 0 && index < (int)threads.size() && threads.size() > 1) {
            threads.erase(threads.begin() + index);
            if (activeThreadIndex >= (int)threads.size()) {
                activeThreadIndex = threads.size() - 1;
            }
        }
    }

    void moveThread(int fromIndex, int toIndex) {
        if (fromIndex < 0 || fromIndex >= (int)threads.size()) return;
        if (toIndex < 0 || toIndex >= (int)threads.size()) return;
        if (fromIndex == toIndex) return;

        AnimThread temp = std::move(threads[fromIndex]);
        threads.erase(threads.begin() + fromIndex);
        threads.insert(threads.begin() + toIndex, std::move(temp));

        // Update active index
        if (activeThreadIndex == fromIndex) {
            activeThreadIndex = toIndex;
        } else if (fromIndex < activeThreadIndex && toIndex >= activeThreadIndex) {
            activeThreadIndex--;
        } else if (fromIndex > activeThreadIndex && toIndex <= activeThreadIndex) {
            activeThreadIndex++;
        }
    }

    AnimThread* getActiveThread() {
        if (activeThreadIndex >= 0 && activeThreadIndex < (int)threads.size()) {
            return &threads[activeThreadIndex];
        }
        return nullptr;
    }

    const AnimThread* getActiveThread() const {
        if (activeThreadIndex >= 0 && activeThreadIndex < (int)threads.size()) {
            return &threads[activeThreadIndex];
        }
        return nullptr;
    }

    void selectThread(int index) {
        if (index >= 0 && index < (int)threads.size()) {
            // Deselect all
            for (auto& t : threads) t.selected = false;
            // Select new
            activeThreadIndex = index;
            threads[index].selected = true;
        }
    }

    // =========================================================================
    // Playback Control
    // =========================================================================

    void play() {
        isPlaying = true;
    }

    void pause() {
        isPlaying = false;
    }

    void togglePlayback() {
        isPlaying = !isPlaying;
    }

    void stop() {
        isPlaying = false;
        for (auto& thread : threads) {
            thread.resetPlayback();
        }
    }

    void update(float deltaTime) {
        if (!isPlaying) return;

        for (auto& thread : threads) {
            if (thread.visible) {
                thread.update(deltaTime, globalSpeed);
            }
        }
    }

    // =========================================================================
    // Utility
    // =========================================================================

    int threadCount() const { return (int)threads.size(); }

    void invalidateAllCaches() {
        for (auto& thread : threads) {
            thread.invalidateAllCaches();
        }
    }

    // Get the longest thread duration
    float getMaxDuration() const {
        float maxDuration = 0;
        for (const auto& thread : threads) {
            maxDuration = std::max(maxDuration, thread.getDuration());
        }
        return maxDuration;
    }
};

// =============================================================================
// Global Weave Instance
// =============================================================================

// Singleton accessor for the current weave
Weave& getCurrentWeave();

} // namespace animation

// =============================================================================
// C API for JIT integration
// =============================================================================

extern "C" {

// Weave management
void anim_weave_init();
void anim_weave_cleanup();
void anim_weave_clear();

// Thread management
int32_t anim_add_thread();
void anim_delete_thread(int32_t index);
void anim_select_thread(int32_t index);
int32_t anim_get_active_thread_index();
int32_t anim_get_thread_count();

// Frame management (operates on active thread)
void anim_add_frame();
void anim_add_frame_after_current();
void anim_delete_current_frame();
void anim_duplicate_current_frame();
void anim_clear_current_frame();
void anim_next_frame();
void anim_prev_frame();
void anim_goto_frame(int32_t index);
int32_t anim_get_current_frame_index();
int32_t anim_get_frame_count();

// Frame change callback (for syncing with external systems like UI wheel)
typedef void (*FrameChangeCallback)(int32_t newFrameIndex);
void anim_set_frame_change_callback(FrameChangeCallback callback);

// Thread properties (operates on active thread)
void anim_set_thread_fps(float fps);
float anim_get_thread_fps();
void anim_set_thread_play_mode(int32_t mode);
int32_t anim_get_thread_play_mode();
void anim_set_thread_visible(bool visible);
bool anim_get_thread_visible();
void anim_set_thread_color(float r, float g, float b);

// Playback control
void anim_play();
void anim_pause();
void anim_toggle_playback();
void anim_stop();
bool anim_is_playing();
void anim_set_global_speed(float speed);
float anim_get_global_speed();
void anim_update(float deltaTime);

// Onion skin
void anim_set_onion_skin_mode(int32_t mode);
int32_t anim_get_onion_skin_mode();
void anim_set_onion_skin_frames(int32_t before, int32_t after);
void anim_set_onion_skin_opacity(float opacity);

// Brush settings (call before anim_begin_stroke)
void anim_set_brush(float size, float hardness, float opacity, float spacing,
                    int32_t shapeTexId, int32_t grainTexId, float grainScale, int shapeInverted,
                    float sizePressure, float opacityPressure,
                    float sizeJitter, float opacityJitter, float rotationJitter, float scatter);
void anim_set_stroke_color(float r, float g, float b, float a);

// Stroke recording (records to current frame of active thread)
void anim_begin_stroke(float x, float y, float pressure);
void anim_add_stroke_point(float x, float y, float pressure);
void anim_end_stroke();
void anim_cancel_stroke();

// Get stroke count for current frame
int32_t anim_get_current_frame_stroke_count();

// Frame rendering - replay strokes to Metal renderer
void anim_render_current_frame();
void anim_render_frame(int32_t frameIndex);
void anim_render_onion_skin();

} // extern "C"
