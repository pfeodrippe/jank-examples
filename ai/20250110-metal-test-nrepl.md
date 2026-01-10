# Metal Test Mode + nREPL Integration

## Problem
Testing GPU frame caching in METAL_TEST_MODE requires a way to switch frames programmatically.

## Solution
1. Created `vybe.app.drawing.metal` namespace that starts nREPL on port 5580
2. Added `call_jank_metal_main()` C++ function to load and call the namespace
3. Modified `metal_test_main()` to initialize jank runtime before entering run loop
4. nREPL now runs successfully in METAL_TEST_MODE

## Files Modified
- `src/vybe/app/drawing/metal.jank` - New namespace for nREPL + frame cache helpers
- `DrawingMobile/drawing_mobile_ios.mm` - Added `call_jank_metal_main()`, init jank in metal_test_main
- `src/vybe/app/drawing/native/metal_renderer.h` - Added frame cache API
- `src/vybe/app/drawing/native/metal_renderer.mm` - Implemented GPU frame caching

## Frame Cache API (Tested Successfully)
```cpp
// C API for GPU frame caching - all functions verified working
bool metal_stamp_init_frame_cache(int maxFrames);  // returns true
bool metal_stamp_cache_frame_to_gpu(int frameIndex);  // returns true
bool metal_stamp_switch_to_cached_frame(int frameIndex);  // returns true
bool metal_stamp_is_frame_cached(int frameIndex);  // returns true/false correctly
void metal_stamp_clear_frame_cache();
```

## Testing via nREPL
Tested successfully from jank nREPL (port 5580):

```clojure
;; Require the header
(require '["vybe/app/drawing/native/metal_renderer.h" :as m :scope ""])

;; Initialize 12-frame cache
(m/metal_stamp_init_frame_cache 12)  ;; => true

;; Cache frames 0, 1, 2
(m/metal_stamp_cache_frame_to_gpu 0)  ;; => true
(m/metal_stamp_cache_frame_to_gpu 1)  ;; => true
(m/metal_stamp_cache_frame_to_gpu 2)  ;; => true

;; Check cached status
(m/metal_stamp_is_frame_cached 0)  ;; => true
(m/metal_stamp_is_frame_cached 3)  ;; => false (not cached yet)

;; Rapid switching (GPU-to-GPU)
(dotimes [_ 10]
  (m/metal_stamp_switch_to_cached_frame 0)
  (m/metal_stamp_switch_to_cached_frame 1)
  (m/metal_stamp_switch_to_cached_frame 2))  ;; completes instantly
```

## Key Learnings
1. METAL_TEST_MODE bypasses jank entirely by default - had to explicitly init jank runtime
2. jank header requires work via compile server: `["header.h" :as alias :scope ""]`
3. Session state persists between nREPL evaluations
4. GPU-to-GPU frame switching is very fast (30 switches completed instantly)

## Next Steps
1. Integrate GPU frame cache into actual frame wheel rotation
2. Replace CPU->GPU texture upload with GPU->GPU switch for cached frames
3. Test visual smoothness during wheel rotation
