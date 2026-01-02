# Loom-like iOS Drawing Animation App - Implementation Plan

## Overview

Create an iOS drawing animation app inspired by Looom (https://www.iorama.studio/looom) using jank as the primary development language. The app will run on iPad and leverage jank's C++ interop for performance-critical rendering while keeping application logic in jank.

## Target Features (MVP to Full)

### MVP (Minimum Viable Product)
1. Drawing canvas with Apple Pencil and touch support
2. Frame-based animation (add/remove/scrub frames)
3. Basic timeline UI with thread reels
4. Simple brush (line mode only)
5. Play/pause controls
6. Undo/redo
7. SVG export

### Phase 2 (Core Animation Features)
1. Onion skinning (2 past, 2 future frames)
2. Multiple threads (up to 5)
3. Color picker per thread
4. Play modes (forward, backward, ping-pong, random)
5. Thread-specific FPS control
6. Thread transforms (pan, pinch, zoom)

### Phase 3 (Advanced Features)
1. Fill brush mode
2. Blend modes (alpha, additive, multiply, invert)
3. Pressure sensitivity (pencil + finger velocity)
4. Bookmark system
5. History scrubbing with pulley
6. Auto step / Auto insert tools
7. Weave-level time scaling

### Phase 4 (Music & Export)
1. BPM time mode
2. MIDI time sync
3. Video recording (screen capture)
4. Background color/image
5. Grid overlay

## Architecture

### Project Structure

```
something/
├── src/
│   └── vybe/
│       └── loom/
│           ├── core.jank         # Main loom namespace
│           ├── canvas.jank       # Drawing canvas state
│           ├── frame.jank        # Frame storage & management
│           ├── thread.jank       # Thread (layer) abstraction
│           ├── weave.jank        # Weave (project) abstraction
│           ├── timeline.jank     # Timeline UI & pulley
│           ├── brush.jank        # Brush engine
│           ├── color.jank        # Color picker UI
│           ├── history.jank      # Undo/redo system
│           ├── export.jank       # SVG/Video export
│           └── time.jank         # Time modes (BPM/MIDI)
│
├── LoomMobile/                    # iOS native code (like SdfViewerMobile)
│   ├── main.mm                   # iOS entry point
│   ├── loom_view_controller.h/m  # Main view controller
│   ├── drawing_view.h/m          # Metal drawing canvas
│   ├── metal_renderer.h/cpp      # Metal rendering layer
│   ├── AppDelegate.swift         # iOS app lifecycle
│   ├── config-common.yml         # Shared XcodeGen config
│   ├── project-loom-jit.yml      # XcodeGen project (JIT mode)
│   └── jank-resources/           # JIT bundle (synced from src/vybe/loom/)
│       └── src/
│           └── jank/
│               └── vybe/
│                   └── loom/     # Synced from src/vybe/loom/
│
└── Makefile                      # Add loom targets here
```

### Core Data Structures

#### Frame
```jank
(defrecord Frame
  [id          ; unique frame identifier
   strokes     ; vector of stroke data
   timestamp   ; creation time
   thumbnail   ; cached thumbnail for UI
   bookmark?   ; is this frame bookmarked?
   bookmark-id ; bookmark identifier
   ])
```

#### Stroke
```jank
(defrecord Stroke
  [points      ; vector of {x, y, pressure, timestamp}
   color       ; RGBA
   thickness   ; base line width
   brush-mode  ; :line or :fill
   opacity     ; 0.0-1.0
   blend-mode  ; :alpha, :additive, :multiply, :invert
   created-at  ; frame index when created
   ])
```

#### Thread (Layer)
```jank
(defrecord Thread
  [id          ; thread ID (0-4)
   name        ; thread name
   color       ; thread color (RGBA)
   visible?    ; visibility toggle
   opacity     ; opacity (0.0-1.0)
   brush-mode  ; :line or :fill
   thickness   ; line thickness
   blend-mode  ; blend mode
   frames      ; map of frame-id -> Frame
   current-id  ; current frame ID
   fps         ; frames per second
   playing?    ; is this thread playing
   play-mode   ; :forward, :backward, :ping-pong, :random
   paused?     ; thread-level pause
   ])
```

#### Weave (Project)
```jank
(defrecord Weave
  [id          ; project ID
   name        ; project name
   threads     ; vector of Thread (max 5)
   background  ; {:color c, :image path, :grid? bool}
   created-at  ; creation timestamp
   modified-at ; modification timestamp
   width       ; canvas width
   height      ; canvas height
   ])
```

### Timeline Architecture

#### Pulley (Time Navigation)
- Circular gesture control for time scrubbing
- Rotation direction determines frame navigation
- Touch + drag = scrub time
- Swipe up/down = frame step

#### Reel UI (Thread Representation)
- Circular visualization of thread timeline
- Dots = frames
- Rotation = thread playhead position
- Tap to select thread
- Long-press for thread edit menu

## Implementation Plan by Phase

### Phase 0: Foundation (Weeks 1-2)

#### 0.1 Create Loom Project Structure
```
1. Create SdfViewerMobile/project-loom.yml
2. Create LoomMobile directory with iOS files
3. Create jank-resources/src/jank/vybe/loom/
4. Set up XcodeGen project generation
```

#### 0.2 Metal Drawing Canvas
```
1. Create LoomMobile/drawing_view.h/m (UIView subclass)
2. Create LoomMobile/metal_renderer.h/cpp
3. Implement Metal device setup
4. Create command buffer for drawing
5. Implement line rendering shader
6. Bridge to jank via C++ interop
```

#### 0.3 Touch/Pencil Input Bridge
```
1. Extend sdf_viewer_ios.mm with drawing events
2. Add vybe_loom_touch_began/moved/ended
3. Pass UITouch/UIPencil events to jank
4. Implement pressure data extraction
5. Implement velocity calculation for finger drawing
```

### Phase 1: Core Drawing & Frames (Weeks 3-4)

#### 1.1 Canvas Namespace (vybe.loom.canvas)
```jank
(ns vybe.loom.canvas
  (:require ["LoomMobile/metal_renderer.h" :as lr :scope ""]))

;; Canvas state
(defonce *canvas-state
  (atom {:size [1024 1024]
         :current-thread 0
         :current-frame-id 0
         :zoom 1.0
         :offset [0 0]
         :dirty? true}))

;; Drawing lifecycle
(defn touch-began [x y pressure]
  ;; Start new stroke
  )

(defn touch-moved [x y pressure]
  ;; Add point to current stroke
  )

(defn touch-ended []
  ;; Finalize stroke
  )

(defn render-frame []
  ;; Render current frame to Metal texture
  )
```

#### 1.2 Frame Management (vybe.loom.frame)
```jank
(ns vybe.loom.frame
  (:require [vybe.loom.thread :as thread]))

;; Frame operations
(defn create-frame!
  "Create a new blank frame after current frame"
  []

(defn remove-frame!
  "Remove current frame"
  []

(defn duplicate-frame!
  "Duplicate current frame"
  []

(defn clear-frame!
  "Clear all strokes from current frame"
  []

(defn get-frame [thread-id frame-id]
  "Get frame by thread and frame ID"
  )

(defn next-frame [thread-id]
  "Get next frame ID based on play mode"
  )

(defn prev-frame [thread-id]
  "Get previous frame ID based on play mode"
  )
```

#### 1.3 Stroke Engine (vybe.loom.brush)
```jank
(ns vybe.loom.brush
  (:require [vybe.loom.canvas :as canvas]))

;; Brush settings
(defonce *brush-settings
  (atom {:mode :line
         :thickness 2.0
         :opacity 1.0
         :blend-mode :alpha
         :pressure-enabled true}))

;; Stroke rendering
(defn add-stroke-point [x y pressure]
  ;; Add point to current stroke
  ;; Apply pressure to thickness if enabled
  )

(defn finalize-stroke []
  ;; Finalize stroke and store in current frame
  )

(defn interpolate-stroke [p1 p2 t]
  ;; Smooth stroke rendering between points
  )
```

### Phase 2: Timeline UI (Weeks 5-6)

#### 2.1 Thread System (vybe.loom.thread)
```jank
(ns vybe.loom.thread)

(defrecord Thread [id name color visible? opacity
                   brush-mode thickness blend-mode
                   frames current-id fps playing?
                   play-mode paused?])

;; Thread operations
(defn create-thread!
  "Create new thread with copy of current thread's properties"
  []

(defn delete-thread!
  "Delete thread at index"
  []

(defn select-thread!
  "Select thread for editing"
  [thread-id]

(defn get-thread
  "Get thread by ID"
  [thread-id]
  )

(defn get-selected-thread
  "Get currently selected thread"
  []
  )
```

#### 2.2 Weave (vybe.loom.weave)
```jank
(ns vybe.loom.weave
  (:require [vybe.loom.thread :as thread]))

(defrecord Weave [id name threads background
                  created-at modified-at width height])

;; Weave operations
(defn create-weave!
  "Create new weave with default thread"
  []

(defn add-thread!
  "Add thread to weave"
  []

(defn remove-thread!
  "Remove thread from weave"
  []

(defn reorder-threads!
  "Reorder threads (for layer stacking)"
  [from-index to-index]

(defn get-active-threads
  "Get all visible threads for rendering"
  []
  )
```

#### 2.3 Timeline UI with ImGui
```jank
(ns vybe.loom.timeline
  (:require [vybe.loom.thread :as thread]
            [vybe.loom.frame :as frame]))

;; Draw reel for thread
(defn draw-thread-reel [thread-id]
  ;; Draw circular reel UI
  ;; Show dots for frames
  ;; Highlight current frame
  )

;; Draw pulley for time navigation
(defn draw-pulley []
  ;; Circular gesture control
  ;; Touch + rotate to scrub
  )

;; Draw timeline toolbar
(defn draw-timeline-toolbar []
  ;; Frame counter
  ;; Play/pause button
  ;; Add/remove frame buttons
  ;; Auto step/auto insert toggles
  )
```

### Phase 3: Playback Engine (Weeks 7-8)

#### 3.1 Animation System (vybe.loom.playback)
```jank
(ns vybe.loom.playback)

;; Global playback state
(defonce *playback-state
  (atom {:playing? false
         :current-time 0.0
         :bpm 120
         :time-mode :normal  ;; :normal, :bpm, :midi
         :midi-connected? false
         }))

;; Playback control
(defn play! []
  ;; Start playback of all visible threads
  )

(defn pause! []
  ;; Pause playback
  )

(defn stop! []
  ;; Stop and reset to beginning
  )

(defn seek! [time]
  ;; Seek to specific time
  )

(defn step-forward! []
  ;; Step to next frame for selected thread(s)
  )

(defn step-backward! []
  ;; Step to previous frame
  )
```

#### 3.2 Frame-by-Frame Rendering
```jank
(defn render-current-frames []
  "Render all active threads at current frame positions"
  (doseq [thread (weave/get-active-threads)]
    (let [frame-id (thread/get-current-frame-id thread)]
      (render/thread-frame thread frame-id))))

(defn onion-skin-render []
  "Render onion skin overlays for past/future frames"
  (when (onion-skin-enabled?)
    (doseq [offset (get-onion-skin-frames)]
      (let [past-frame (thread/get-frame-offset thread -offset)
            future-frame (thread/get-frame-offset thread offset)]
        (render/onion-skin past-frame :past)
        (render/onion-skin future-frame :future)))))
```

### Phase 4: Color & Brush Tools (Weeks 9-10)

#### 4.1 Color Picker (vybe.loom.color)
```jank
(ns vybe.loom.color
  (:require [vybe.loom.thread :as thread]))

;; Color picker implementation
(defn draw-color-picker []
  ;; Draw circular color wheel
  ;; Hue ring + saturation/brightness triangle
  ;; Opacity slider
  )

(defn rgb-to-hsv [r g b]
  ;; Convert RGB to HSV
  )

(defn hsv-to-rgb [h s v]
  ;; Convert HSV to RGB
  )
```

#### 4.2 Brush System Enhancement
```jank
;; Line brush
(defn render-line-stroke [stroke]
  ;; Render stroke as lines
  ;; Apply thickness, color, opacity, blend mode
  )

;; Fill brush
(defn render-fill [path color]
  ;; Render fill for closed paths
  ;; Flood fill or shape fill based on mode
  )

;; Pressure sensitivity
(defn calculate-thickness [base-thickness pressure velocity]
  ;; Apply pressure curve
  ;; Apply velocity tapering for finger drawing
  )
```

### Phase 5: Undo/Redo History (Week 11)

#### 5.1 History System (vybe.loom.history)
```jank
(ns vybe.loom.history)

(defonce *history
  (atom {:undo-stack []
         :redo-stack []
         :max-size 50}))

(defn snapshot!
  "Take snapshot of current state before change"
  []

(defn undo! []
  "Undo last action"
  []

(defn redo! []
  "Redo last undone action"
  []

(defn clear-history! []
  "Clear undo/redo stacks"
  []
  )
```

#### 5.2 History Scrubbing with Pulley
```jank
(defn draw-history-pulley []
  ;; Show history timeline around pulley
  ;; Rotate to scrub through history states
  )
```

### Phase 6: Export & Integration (Weeks 12-14)

#### 6.1 SVG Export (vybe.loom.export.svg)
```jank
(ns vybe.loom.export.svg)

(defn export-frame [thread frame-id output-path]
  "Export single frame as SVG"
  []

(defn export-weave [weave output-path]
  "Export entire weave as SVG with layer groups"
  []

(defn export-animation [weave fps output-path]
  "Export animation as animated SVG or sprite sheet"
  []
  )
```

#### 6.2 Video Recording (vybe.loom.export.video)
```jank
(ns vybe.loom.export.video)

(defn start-recording! []
  "Start screen capture recording"
  []

(defn stop-recording! []
  "Stop and save recording to Photos"
  []

(defn record-frame []
  "Capture current frame to video buffer"
  )
```

## C++ Interop Strategy

### Header Requires Pattern

```jank
;; For Metal rendering
["LoomMobile/metal_renderer.h" :as mr :scope ""]

;; For touch event bridging
["LoomMobile/touch_bridge.h" :as tb :scope ""]

;; For video recording
["LoomMobile/video_recorder.h" :as vr :scope ""]
```

### cpp/ API Usage

```jank
;; Creating Metal objects
(let [device (mr/MTLCreateSystemDefaultDevice)]
  (cpp/box device))

;; Calling Metal APIs
(mr/MTLCommandBufferPresent (cpp/unbox command-buffer))

;; Pointer manipulation for textures
(let [texture-ptr (cpp/& (cpp/.-texture texture-box))]
  (mr/MTLBlitEncoderCopyBytes encoder texture-ptr source-ptr size))
```

### C++ Bridge Files

#### LoomMobile/metal_renderer.h
```cpp
#pragma once
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

void loom_metal_init(void* view);
void loom_metal_clear(float r, float g, float b, float a);
void loom_metal_draw_line(float x1, float y1, float x2, float y2,
                          float thickness, float* color);
void loom_metal_draw_stroke(void* stroke_data, int point_count,
                            float base_thickness, float* color);
void loom_metal_present();
void* loom_metal_get_current_texture();
```

#### LoomMobile/touch_bridge.h
```cpp
#pragma once

extern "C" {
void loom_touch_began(float x, float y, float pressure, int64_t timestamp);
void loom_touch_moved(float x, float y, float pressure, int64_t timestamp);
void loom_touch_ended(float x, float y, float velocity, int64_t timestamp);
void loom_touch_cancelled();
}
```

## iOS Platform Considerations

### Following Existing SdfViewerMobile Pattern

The Loom app follows the same structure as the existing iOS app:

| Component | Location | Notes |
|-----------|----------|-------|
| iOS native code | `LoomMobile/` | Like `SdfViewerMobile/` |
| jank source | `src/vybe/loom/` | Like `src/vybe/sdf/` |
| JIT bundle | `LoomMobile/jank-resources/` | Synced from `src/vybe/loom/` |
| XcodeGen config | `LoomMobile/config-common.yml` | Extends root config-common.yml |
| Project config | `LoomMobile/project-loom.yml` | Uses JITResources template |
| Build targets | `Makefile` | Added alongside existing targets |

### JIT Mode Source Sync

For JIT development, Loom sources are synced from `src/vybe/loom/` to `LoomMobile/jank-resources/src/jank/vybe/loom/` via `make loom-ios-sync-sources` (similar to how `make ios-jit-sync-sources` works for SdfViewerMobile).

### Touch Input
- UITouch for finger input (velocity-based thickness)
- UIPencilPaper for Apple Pencil (pressure + tilt)
- Touch coalescing for smooth strokes
- Prediction for reduced latency

### Display
- ProMotion display support (120Hz)
- Retina resolution handling
- Metal coordinate system (top-left origin)

### Storage
- iOS Documents directory for projects
- SVG file sharing via Files app
- Video export to Photos library

### Performance
- Background thread for stroke processing
- Texture caching for frame thumbnails
- Level-of-detail for onion skins
- Metal performance counters

## Build System

### XcodeGen Configuration (LoomMobile/project-loom.yml)

Following SdfViewerMobile pattern:

```yaml
name: LoomMobile
include:
  - config-common.yml

targets:
  LoomMobile:
    templates: [CommonSources, JITResources]
    settings:
      groups: [CommonHeaders, CommonCodeSign]
      base:
        INFOPLIST_FILE: Info.plist
        PRODUCT_BUNDLE_IDENTIFIER: com.vybe.loom
        TARGETED_DEVICE_FAMILY: "2"  # iPad only
        ASSETCATALOG_COMPILER_APPICON_NAME: AppIcon
        CODE_SIGN_ENTITLEMENTS: loom-jit.entitlements
        OTHER_LDFLAGS:
          - "-framework Metal"
          - "-framework MetalKit"
          - "-framework QuartzCore"
          - "-framework AVFoundation"
          - "-framework Photos"
          - "-framework UIKit"
          - "-framework Foundation"
          - "-framework CoreGraphics"
          - "-framework CoreHaptics"
          - "-lc++"
    dependencies:
      - sdk: Metal.framework
      - sdk: MetalKit.framework
      - sdk: AVFoundation.framework
      - sdk: Photos.framework
```

### Build System - Makefile Targets

Add to existing `Makefile`:

```makefile
# Loom iOS targets (following SdfViewerMobile pattern)
.PHONY: loom-ios-jit-sim loom-ios-jit-device loom-ios-setup

# Sync loom sources to iOS bundle (like ios-jit-sync-sources)
.PHONY: loom-ios-sync-sources
loom-ios-sync-sources:
	@echo "Syncing loom sources to iOS bundle..."
	@mkdir -p LoomMobile/jank-resources/src/jank/vybe
	@rsync -av --delete --include='*.jank' --include='*/' --exclude='*' src/vybe/loom/ LoomMobile/jank-resources/src/jank/vybe/loom/
	@echo "Loom sources synced!"

# Generate loom Xcode project (JIT simulator)
.PHONY: loom-ios-project
loom-ios-project: loom-ios-sync-sources
	@echo "Generating Loom Xcode project..."
	cd LoomMobile && ./generate-project.sh
	@echo "Xcode project generated: LoomMobile/LoomMobile.xcodeproj"

# Build Loom JIT for simulator
.PHONY: loom-ios-jit-sim
loom-ios-jit-sim: loom-ios-project
	@echo "Building Loom for iOS Simulator (JIT)..."
	cd LoomMobile && xcodebuild \
		-project LoomMobile.xcodeproj \
		-scheme LoomMobile \
		-configuration Debug \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
		build

# Run Loom in simulator
.PHONY: loom-ios-sim-run
loom-ios-sim-run: loom-ios-jit-sim
	@echo "Launching simulator..."
	xcrun simctl boot 'iPad Pro 13-inch (M4)' 2>/dev/null || true
	open -a Simulator
	xcrun simctl install 'iPad Pro 13-inch (M4)' $$(find ~/Library/Developer/Xcode/DerivedData -name "LoomMobile.app" -path "*/Build/Products/Debug-iphonesimulator/*" ! -path "*/Index.noindex/*" 2>/dev/null | head -1)
	xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.loom
```

## Testing Strategy

### Unit Tests (jank)
- Frame creation/deletion
- Thread operations
- Color conversion
- History snapshots

### Integration Tests
- Drawing workflow (touch to stroke to render)
- Timeline navigation (pulley scrubbing)
- Playback sync
- Export verification

### Performance Tests
- Stroke rendering latency
- Frame switching time
- Memory usage with many frames
- Battery impact

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Metal learning curve | High | Start with simple line rendering, iterate |
| Performance on older iPads | Medium | Optimize onion skins, use LOD |
| iOS API complexity | Medium | Use proven patterns from SdfViewerMobile |
| SVG export complexity | Low | Start with single frame, iterate to animation |
| Memory pressure with frames | High | Implement frame eviction, thumbnail caching |

## Success Metrics

1. **Performance**: 60fps drawing with <16ms latency
2. **Usability**: Complete drawing workflow in <2 minutes
3. **Compatibility**: Works on iPad Pro (2020+) and iPad Air (2020+)
4. **Export**: SVG files import correctly in Adobe Animate
5. **Quality**: 5-star App Store rating target

## Next Steps

1. Create project structure and XcodeGen configuration
2. Implement Metal drawing canvas with line rendering
3. Bridge touch/pencil events to jank
4. Implement frame storage and basic playback
5. Build timeline UI with thread reels
6. Add color picker and brush settings
7. Implement onion skinning
8. Add SVG export
9. Polish UI and add polish features
10. Beta test on physical devices

## References

- Looom User Guide: https://www.iorama.studio/looom-user-guide
- Looom App Store: https://apps.apple.com/us/app/looom/id1454153126
- Metal Programming Guide: https://developer.apple.com/metal/
- Apple Pencil APIs: https://developer.apple.com/documentation/uikit/pencil_interactions
- jank C++ Interop: ai/20251202-native-resources-guide.md
- Existing SdfViewerMobile: SdfViewerMobile/sdf_viewer_ios.mm
