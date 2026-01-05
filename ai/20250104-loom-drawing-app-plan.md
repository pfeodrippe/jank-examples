# Loom-like Drawing & Animation App Implementation Plan

**Date**: 2025-01-04
**Goal**: Build a Looom-inspired hand-drawn animation app with Procreate-like drawing experience

## Research Summary

### Looom's Core Design Philosophy

From [Apple's Behind the Design](https://developer.apple.com/news/?id=s26ze13m) and [iorama.studio](https://www.iorama.studio/looom):

> "Flow first — experience first. That is the core of the design thought of Looom."

Key insights:
- **Not a production tool** - It's an "animation playground" / "next-gen flipbook"
- **Instrument metaphor** - "like a small synthesizer for animation"
- **Minimal UI** - Controls at edges, canvas dominates, no accent colors
- **Progressive disclosure** - Interface looks featureless but reveals depth on demand
- **Multi-hand interaction** - Apple Pencil draws while free hand controls time via touch

### Looom's Technical Architecture

From [Looom User Guide](https://www.iorama.studio/looom-user-guide):

| Concept | Description |
|---------|-------------|
| **Thread** | A single layer with its own timeline, FPS, color |
| **Weave** | Collection of up to 5 threads (the full project) |
| **Pulley** | Gesture-based time scrubbing (finger on canvas) |
| **Reel** | Visual representation of a thread (circular with frame dots) |

**Timeline System**:
- Each thread has **independent frame count and FPS**
- Play modes: Forward, Backward, Ping-pong, Random
- Music Time mode: BPM-based playback
- MIDI sync support for external sequencers

**Drawing**:
- Two modes: Line (stroke with width slider) or Fill
- Single color per thread
- Pressure/velocity sensitivity
- Onion skin (2 past, 2 future, or mixed)

**Export**:
- Native SVG format (crispy vectors)
- Adobe Animate integration via JSFL script

---

## Implementation Architecture

### CRITICAL RULE: jank-first, C++ only when absolutely necessary!

**What MUST be in C++ (native Metal/Vulkan)**:
- GPU triangle rendering (submit pre-computed vertex buffers)
- Raw SDL event capture (already exists in sdf_engine.hpp)
- Touch pressure capture (small addition to existing code)

**What MUST be in jank (99% of code)**:
- ALL data structures (Stroke, Frame, Thread, Weave)
- ALL math (bezier smoothing, stroke→triangles tessellation)
- ALL state management (atoms, undo/redo)
- ALL input processing logic
- ALL animation/timeline logic

**ImGui usage**: ONLY for menus, debug panels. **NOT for canvas drawing!**

### Namespace Structure

```
vybe.app.drawing           ; Main entry point - ALL JANK
├── vybe.app.drawing.core  ; Data structures - ALL JANK
├── vybe.app.drawing.math  ; Bezier math, tessellation - ALL JANK
├── vybe.app.drawing.input ; Touch/Pencil processing - ALL JANK
├── vybe.app.drawing.timeline ; Playback, timing - ALL JANK
├── vybe.app.drawing.ui    ; ImGui menus ONLY - ALL JANK
├── vybe.app.drawing.export; SVG export - ALL JANK
└── vybe.app.drawing.state ; App state atoms - ALL JANK
```

### Tech Stack Integration

```
┌──────────────────────────────────────────────┐
│            jank Layer (99% of code)          │
│  vybe.app.drawing.* - ALL app logic          │
│  - Data structures, bezier math, state       │
│  - Tessellation: stroke → triangle vertices  │
│  - Animation timing, playback logic          │
│  - ImGui UI (menus/debug ONLY, not canvas!)  │
└──────────────────────────────────────────────┘
                      │
          (pre-computed triangle vertices)
                      ▼
┌──────────────────────────────────────────────┐
│      C++ Bridge (MINIMAL - <50 lines)        │
│  Additions to sdf_engine.hpp:                │
│  - draw_triangles_2d(verts, count)           │
│  - get_event_pressure(idx)                   │
│  Just submits jank-computed triangles to GPU │
└──────────────────────────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────┐
│           Platform (iOS/macOS)               │
│  - SDL3 for window/events                    │
│  - Vulkan/MoltenVK for GPU rendering         │
└──────────────────────────────────────────────┘
```

### Key Design: Tessellation happens in jank!

```clojure
;; vybe.app.drawing.math - ALL IN JANK!
(defn stroke->triangles [stroke]
  "Convert stroke to GPU-ready triangle vertices.
   Returns flat vector of floats: [x1 y1 r1 g1 b1 a1, x2 y2 ...]"
  (let [beziers (points->beziers (:points stroke))
        width (:width stroke)
        color (:color stroke)]
    (->> beziers
         (subdivide-curves 8)        ; smooth curves
         (compute-perpendiculars)    ; for line thickness
         (offset-points width)       ; create ribbon
         (triangulate)               ; ribbon → triangles
         (add-vertex-colors color)
         (flatten))))

;; C++ receives finished triangles - MINIMAL!
(sdfx/draw_triangles_2d vertex-data (count vertex-data))
```

---

## Core Data Structures

### Stroke (vybe.app.drawing.core)

```clojure
;; A single stroke is a sequence of sample points
(defrecord StrokePoint
  [x       ; float - canvas position
   y       ; float
   pressure ; float 0.0-1.0 (from Apple Pencil or velocity)
   timestamp]) ; long - for velocity calculation

(defrecord Stroke
  [id        ; uuid
   points    ; vector of StrokePoint
   color     ; [r g b a]
   width     ; base width (modified by pressure)
   fill?])   ; boolean - line or fill mode

;; Rendering produces bezier control points from raw samples
(defrecord BezierSegment
  [p0 p1 p2 p3   ; control points
   w0 w1])       ; width at start/end
```

### Frame & Thread

```clojure
(defrecord Frame
  [id       ; int - frame index within thread
   strokes  ; vector of Stroke
   dirty?]) ; boolean - needs re-render to texture

(defrecord Thread
  [id          ; uuid
   name        ; string
   frames      ; vector of Frame (each "dot" on the reel)
   frame-count ; int - number of frames in this thread
   current-frame ; int - which frame we're viewing/editing
   fps         ; float - playback speed
   color       ; [r g b a] - single color for entire thread
   line-width  ; float - stroke width
   fill-mode?  ; boolean - line or fill
   opacity     ; float 0.0-1.0
   blend-mode  ; :alpha :additive :multiply :invert
   visible?    ; boolean
   playing?    ; boolean
   play-mode]) ; :forward :backward :pingpong :random

(defrecord Weave
  [id       ; uuid
   name     ; string
   threads  ; vector of Thread (max 5)
   active-thread ; int - which thread is selected
   bg-color ; [r g b a]
   global-speed ; float - multiplier (1.0x)
   time-mode]) ; :standard :bpm :midi
```

---

## Phase 1: Basic Drawing Canvas (Foundation)

### Goal
Single-layer canvas with smooth bezier stroke rendering and Apple Pencil support.

### 1.1 Metal Stroke Renderer (C++)

Create `drawing_engine.hpp` with:

```cpp
namespace drawing {
    // Initialize Metal rendering context
    void init(void* metalLayer);
    void cleanup();

    // Begin/end frame
    void begin_frame();
    void end_frame();

    // Stroke rendering
    void set_stroke_color(float r, float g, float b, float a);
    void set_stroke_width(float width);
    void begin_stroke();
    void add_stroke_point(float x, float y, float pressure);
    void end_stroke();

    // Render all strokes to current framebuffer
    void render_strokes();

    // Touch input (returns normalized coords)
    bool poll_touch_event(float* x, float* y, float* pressure, int* phase);
}
```

**Key implementation details** (learned from [ios_metal_bezier_renderer](https://github.com/eldade/ios_metal_bezier_renderer)):

1. **GPU-based bezier tessellation** - Calculate curve vertices in vertex shader for performance
2. **Triangle strip rendering** - Convert strokes to thick lines via perpendicular offsetting
3. **Variable width** - Use pressure/velocity to modulate perpendicular offset
4. **Catmull-Rom smoothing** - Convert raw touch samples to smooth bezier curves

### 1.2 jank Stroke Management

```clojure
(ns vybe.app.drawing.render)

;; Called from input handling
(defn start-stroke! [x y pressure]
  (let [point (->StrokePoint x y pressure (System/currentTimeMillis))]
    (swap! state/*current-stroke
           #(-> %
                (assoc :points [point])
                (assoc :id (random-uuid))))))

(defn add-point! [x y pressure]
  (let [point (->StrokePoint x y pressure (System/currentTimeMillis))]
    (swap! state/*current-stroke
           update :points conj point)))

(defn end-stroke! []
  (let [stroke @state/*current-stroke]
    (when (> (count (:points stroke)) 1)
      ;; Add to current frame
      (state/add-stroke-to-current-frame! stroke)
      ;; Send to C++ for rendering
      (render-stroke-to-gpu! stroke))
    (reset! state/*current-stroke nil)))
```

### 1.3 Input Handling

```clojure
(ns vybe.app.drawing.input)

(def ^:private +touch-began+ 0)
(def ^:private +touch-moved+ 1)
(def ^:private +touch-ended+ 2)

(defn process-touch-events! []
  (loop []
    (when-let [[x y pressure phase] (drawing-engine/poll-touch-event)]
      (case phase
        0 (render/start-stroke! x y pressure)
        1 (render/add-point! x y pressure)
        2 (render/end-stroke!))
      (recur))))
```

### Deliverables
- [ ] `SdfViewerMobile/drawing_engine.hpp` - Metal rendering engine
- [ ] `SdfViewerMobile/drawing_engine_impl.mm` - Metal implementation
- [ ] `src/vybe/app/drawing/core.jank` - Data structures
- [ ] `src/vybe/app/drawing/render.jank` - Stroke rendering logic
- [ ] `src/vybe/app/drawing/input.jank` - Touch/Pencil handling
- [ ] `src/vybe/app/drawing/state.jank` - App state atoms
- [ ] Basic canvas with pressure-sensitive drawing working

---

## Phase 2: Frame-by-Frame Animation

### Goal
Multi-frame timeline with onion skin preview.

### 2.1 Frame Buffer Management

Each frame gets rendered to an offscreen texture for fast playback:

```cpp
namespace drawing {
    // Frame management
    int create_frame();  // Returns frame ID
    void delete_frame(int frame_id);
    void set_active_frame(int frame_id);

    // Render strokes to frame's texture
    void bake_frame(int frame_id);

    // Get frame as texture (for playback/preview)
    void* get_frame_texture(int frame_id);

    // Onion skin
    void set_onion_skin_mode(int mode);  // 0=off, 1=past, 2=future, 3=both
    void render_onion_skin(int* frame_ids, int count, float* opacities);
}
```

### 2.2 Timeline Playback

```clojure
(ns vybe.app.drawing.timeline)

(defn advance-frame! [thread direction]
  (let [current (:current-frame thread)
        count (:frame-count thread)
        next-frame (case (:play-mode thread)
                     :forward (mod (inc current) count)
                     :backward (mod (dec current) count)
                     :pingpong (pingpong-next current count direction)
                     :random (rand-int count))]
    (state/set-current-frame! (:id thread) next-frame)))

(defn update-playback! [dt]
  (doseq [thread (state/get-playing-threads)]
    (let [frame-duration (/ 1000.0 (:fps thread))]
      (when (> (state/get-thread-elapsed thread) frame-duration)
        (advance-frame! thread :forward)
        (state/reset-thread-elapsed! thread)))))
```

### 2.3 Onion Skin Rendering

```clojure
(defn get-onion-frames [thread mode]
  (let [current (:current-frame thread)
        count (:frame-count thread)]
    (case mode
      :past [(max 0 (- current 2)) (max 0 (dec current))]
      :future [(min (dec count) (inc current)) (min (dec count) (+ current 2))]
      :both [(max 0 (dec current)) (min (dec count) (inc current))]
      :off [])))
```

### Deliverables
- [ ] Frame texture management in C++
- [ ] `src/vybe/app/drawing/timeline.jank` - Playback logic
- [ ] Onion skin preview rendering
- [ ] Auto-step & auto-insert modes
- [ ] Frame add/delete operations

---

## Phase 3: Thread/Layer System (Looom's "Weaving")

### Goal
Multiple independent timelines (threads) that loop at different rates.

### 3.1 Thread Management

```clojure
(ns vybe.app.drawing.core)

(defn create-thread [weave-id color]
  (let [thread (->Thread
                 (random-uuid)
                 "Thread 1"
                 [(create-empty-frame)]  ; Start with 1 frame
                 1    ; frame-count
                 0    ; current-frame
                 8.0  ; fps
                 color
                 4.0  ; line-width
                 false ; fill-mode
                 1.0  ; opacity
                 :alpha ; blend-mode
                 true ; visible
                 false ; playing
                 :forward)] ; play-mode
    (state/add-thread! weave-id thread)
    thread))

(defn polyrhythmic-update! [dt]
  ;; Each thread advances independently based on its own FPS
  ;; This creates the "polyrhythmic" looping effect
  (doseq [thread (state/get-all-threads)]
    (when (:playing? thread)
      (let [ms-per-frame (/ 1000.0 (:fps thread))
            elapsed (+ (state/get-elapsed thread) dt)]
        (if (>= elapsed ms-per-frame)
          (do
            (advance-frame! thread)
            (state/set-elapsed! thread (- elapsed ms-per-frame)))
          (state/set-elapsed! thread elapsed))))))
```

### 3.2 Reel UI (Circular Thread Visualization)

The iconic Looom "reel" showing frames as dots around a circle:

```clojure
(ns vybe.app.drawing.ui.reel)

(defn draw-reel [thread cx cy radius]
  (let [frames (:frames thread)
        count (count frames)
        angle-step (/ (* 2 Math/PI) count)]
    ;; Draw the circular reel
    (ui/draw-circle cx cy radius (:color thread) 2.0)

    ;; Draw frame dots
    (doseq [[i frame] (map-indexed vector frames)]
      (let [angle (- (* i angle-step) (/ Math/PI 2))  ; Start at top
            dot-x (+ cx (* radius (Math/cos angle)))
            dot-y (+ cy (* radius (Math/sin angle)))
            is-current (= i (:current-frame thread))]
        (ui/draw-circle-filled dot-x dot-y
                               (if is-current 8 4)
                               (if is-current [1 1 1 1] (:color thread)))))

    ;; Draw play head indicator
    (when (:playing? thread)
      (let [angle (- (* (:current-frame thread) angle-step) (/ Math/PI 2))
            hand-x (+ cx (* radius 0.7 (Math/cos angle)))
            hand-y (+ cy (* radius 0.7 (Math/sin angle)))]
        (ui/draw-line cx cy hand-x hand-y [1 1 1 1] 2.0)))))
```

### 3.3 Pulley Gesture (Time Scrubbing)

The pulley appears when touching the canvas (not with pencil):

```clojure
(ns vybe.app.drawing.input)

(defn handle-pulley-gesture [touch-x touch-y]
  (when (and (finger-touch? touch-x touch-y)  ; Not Apple Pencil
             (not @state/*drawing?))
    (let [center-x (/ (state/get-canvas-width) 2)
          center-y (/ (state/get-canvas-height) 2)
          angle (Math/atan2 (- touch-y center-y) (- touch-x center-x))
          delta-angle (- angle @state/*last-pulley-angle)]
      ;; Rotate all active threads proportionally
      (when (> (Math/abs delta-angle) 0.01)
        (doseq [thread (state/get-active-threads)]
          (let [frames-per-rotation (:frame-count thread)
                frame-delta (* (/ delta-angle (* 2 Math/PI)) frames-per-rotation)]
            (state/adjust-frame-by! thread frame-delta))))
      (reset! state/*last-pulley-angle angle))))
```

### Deliverables
- [ ] Multi-thread state management
- [ ] `src/vybe/app/drawing/ui/reel.jank` - Circular reel widget
- [ ] Pulley gesture for time scrubbing
- [ ] Per-thread color, FPS, play mode
- [ ] Thread add/remove/reorder
- [ ] Thread-specific controls (visible, opacity, blend mode)

---

## Phase 4: UI Polish & Controls

### Goal
Minimal, Looom-like interface with progressive disclosure.

### 4.1 UI Layout

```
┌─────────────────────────────────────────────────────┐
│ [←] [→]                           [⏯] [🔧]          │  <- Top bar (minimal)
├─────────────────────────────────────────────────────┤
│                                                     │
│                                                     │
│                    CANVAS                           │
│                                                     │
│                                                     │
│                                   ╭───╮             │
│                                   │ R │  <- Reel   │
│                                   │ E │    widgets │
│                                   │ E │    (one    │
│                                   │ L │    per     │
│                                   │ S │    thread) │
│                                   ╰───╯             │
├─────────────────────────────────────────────────────┤
│ [+frame] [-frame] [🧅onion] [✏️/🎨]    [1.0x speed] │  <- Bottom bar
└─────────────────────────────────────────────────────┘
```

### 4.2 Color Picker (Looom-style)

Looom uses a hue ring with triangular shade picker:

```clojure
(ns vybe.app.drawing.ui.color-picker)

(defn draw-hue-ring [cx cy outer-r inner-r]
  ;; Draw colored ring around edge
  (doseq [i (range 360)]
    (let [angle (* i (/ Math/PI 180))
          [r g b] (hsl-to-rgb i 1.0 0.5)]
      (ui/draw-arc-segment cx cy outer-r inner-r angle (+ angle 0.02) [r g b 1]))))

(defn draw-shade-triangle [cx cy size hue]
  ;; Triangular picker inside the hue ring
  ;; Top: pure hue, bottom-left: black, bottom-right: white
  ...)
```

### 4.3 Thread Edit Panel

Appears when tapping a reel:

```clojure
(defn draw-thread-edit-panel [thread]
  (ui/panel {:x 20 :y 100 :w 300}
    ;; Color
    (color-picker/draw (:color thread) #(state/set-thread-color! thread %))

    ;; Line/Fill toggle
    (ui/toggle "Mode" (:fill-mode? thread) #(state/toggle-fill-mode! thread))

    ;; Width slider
    (ui/slider "Width" (:line-width thread) 1 20 #(state/set-line-width! thread %))

    ;; Opacity slider
    (ui/slider "Opacity" (:opacity thread) 0 1 #(state/set-opacity! thread %))

    ;; FPS slider
    (ui/slider "FPS" (:fps thread) 1 24 #(state/set-fps! thread %))

    ;; Play mode
    (ui/segmented ["→" "←" "↔" "?"]
                  (:play-mode thread)
                  [:forward :backward :pingpong :random]
                  #(state/set-play-mode! thread %))))
```

### Deliverables
- [ ] `src/vybe/app/drawing/ui.jank` - Main UI framework
- [ ] `src/vybe/app/drawing/ui/color_picker.jank` - Hue ring picker
- [ ] `src/vybe/app/drawing/ui/controls.jank` - Buttons, sliders, toggles
- [ ] `src/vybe/app/drawing/ui/thread_panel.jank` - Thread edit panel
- [ ] Onion skin mode toggle
- [ ] Auto-step / Auto-insert toggles
- [ ] Global speed control

---

## Phase 5: Export & Persistence

### Goal
SVG export and project save/load.

### 5.1 SVG Export

```clojure
(ns vybe.app.drawing.export)

(defn stroke-to-svg-path [stroke]
  (let [points (:points stroke)
        beziers (points-to-beziers points)]
    (str "M " (format "%.2f %.2f" (:x (first points)) (:y (first points)))
         (apply str
           (for [seg beziers]
             (format " C %.2f,%.2f %.2f,%.2f %.2f,%.2f"
                     (:x (:p1 seg)) (:y (:p1 seg))
                     (:x (:p2 seg)) (:y (:p2 seg))
                     (:x (:p3 seg)) (:y (:p3 seg))))))))

(defn frame-to-svg [frame thread-color]
  (str "<g id=\"frame-" (:id frame) "\">\n"
       (apply str
         (for [stroke (:strokes frame)]
           (format "  <path d=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\" fill=\"none\"/>\n"
                   (stroke-to-svg-path stroke)
                   (color-to-hex thread-color)
                   (:width stroke))))
       "</g>\n"))

(defn weave-to-svg [weave]
  (let [width 1024 height 1024]
    (str "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         (format "<svg viewBox=\"0 0 %d %d\" xmlns=\"http://www.w3.org/2000/svg\">\n" width height)
         ;; One layer per thread
         (apply str
           (for [thread (:threads weave)]
             (str (format "  <g id=\"%s\" opacity=\"%.2f\">\n" (:name thread) (:opacity thread))
                  (apply str (map #(frame-to-svg % (:color thread)) (:frames thread)))
                  "  </g>\n")))
         "</svg>")))
```

### 5.2 Project Persistence

```clojure
(defn save-weave [weave path]
  (let [data (pr-str weave)]  ; EDN serialization
    (spit path data)))

(defn load-weave [path]
  (let [data (slurp path)]
    (edn/read-string data)))
```

### Deliverables
- [ ] `src/vybe/app/drawing/export.jank` - SVG export
- [ ] Project save/load (EDN format)
- [ ] Video export (frame sequence → ffmpeg)
- [ ] Clipboard copy (SVG)

---

## Implementation Priorities

### MVP (Minimum Viable Product)
1. **Phase 1** - Basic canvas with smooth drawing ← START HERE
2. **Phase 2** - Frame-by-frame (2-3 frames)
3. **Phase 3** - Single thread playback

### V1.0
4. Phase 3 complete - Multi-thread system
5. Phase 4 - Full UI
6. Phase 5 - SVG export

### Future
- MIDI/BPM sync
- More blend modes
- Brush textures
- Import images as reference
- Collaboration/sharing

---

## Technical Notes

### Apple Pencil Pressure

From [Apple's documentation](https://developer.apple.com/documentation/uikit/uibezierpath), pressure is accessed via:

```objc
UITouch* touch = ...;
CGFloat pressure = touch.force / touch.maximumPossibleForce;  // Normalized 0-1
```

For non-pressure devices, use velocity:

```clojure
(defn velocity-to-pressure [p1 p2 dt]
  (let [dist (distance p1 p2)
        velocity (/ dist dt)]
    ;; Invert: fast = thin, slow = thick
    (- 1.0 (min 1.0 (/ velocity max-velocity)))))
```

### Bezier Smoothing (Catmull-Rom → Bezier)

Convert raw touch samples to smooth beziers:

```clojure
(defn catmull-rom-to-bezier [p0 p1 p2 p3]
  ;; Control points for cubic bezier equivalent
  (let [c1 (v+ p1 (v* (v- p2 p0) (/ 1 6)))
        c2 (v- p2 (v* (v- p3 p1) (/ 1 6)))]
    [p1 c1 c2 p2]))
```

### Performance Targets

| Metric | Target |
|--------|--------|
| Touch latency | < 16ms |
| Stroke render | < 5ms |
| Frame bake | < 10ms |
| Playback | 60 FPS UI, variable animation FPS |

---

## Commands Executed

```bash
# Research phase
# - Fetched iorama.studio/looom
# - Fetched iorama.studio/looom-user-guide
# - Fetched Apple Developer "Behind the Design" article
# - Fetched github.com/eldade/ios_metal_bezier_renderer
# - Fetched github.com/Harley-xk/MaLiang
# - Web searched for bezier curve implementations
# - Read project structure and existing iOS code
```

## Next Steps

1. **Create `src/vybe/app/drawing.jank`** - Main entry point namespace
2. **Create `src/vybe/app/drawing/core.jank`** - Data structures
3. **Create `SdfViewerMobile/drawing_engine.hpp`** - Metal C++ header
4. **Create `SdfViewerMobile/drawing_engine_impl.mm`** - Metal implementation
5. **Wire up basic canvas rendering and touch input**
6. **Test pressure sensitivity with Apple Pencil**

---

## Sources

- [Looom Official](https://www.iorama.studio/looom)
- [Looom User Guide](https://www.iorama.studio/looom-user-guide)
- [Apple: Behind the Design - Looom](https://developer.apple.com/news/?id=s26ze13m)
- [9to5Mac: Looom Review](https://9to5mac.com/2020/03/26/looom-ipad-app-review/)
- [CDM: Looom Major Update](https://cdm.link/looom-for-ios-makes-animating-like-playing-a-musical-instrument-now-with-major-update/)
- [iOS Metal Bezier Renderer](https://github.com/eldade/ios_metal_bezier_renderer)
- [MaLiang Drawing Library](https://github.com/Harley-xk/MaLiang)
- [Drawing Bézier Curves (Bartosz Ciechanowski)](https://ciechanow.ski/drawing-bezier-curves/)
- [UIBezierPath Documentation](https://developer.apple.com/documentation/uikit/uibezierpath)
