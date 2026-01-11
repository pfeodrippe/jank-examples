# Visual iOS Testing Implementation Plan

## Problem Statement

We need automated visual testing for the iOS drawing app to:
1. Verify undo/redo produces identical visual results
2. Catch visual regressions in brush rendering
3. Test frame switching preserves canvas state
4. Validate that replay produces identical strokes

## Research Summary

### Available Approaches

#### 1. Swift Snapshot Testing (Recommended for Unit Tests)
- **Library**: [pointfreeco/swift-snapshot-testing](https://github.com/pointfreeco/swift-snapshot-testing)
- **Pros**: First-class Xcode support, image diffs as XCTest attachments, precision control
- **Cons**: Requires XCTest target, runs in simulator
- **How it works**: Pixel-by-pixel comparison using `memcmp()` on CGContextRefs

#### 2. simctl Command Line (Recommended for Integration Tests)
- **Command**: `xcrun simctl io booted screenshot <path>`
- **Pros**: Works with any app, no code changes needed, can be scripted
- **Cons**: Requires running simulator, external comparison tool needed
- **Options**: `--type=png|jpeg|tiff`, `--mask=black|alpha|ignored`

#### 3. MCP iOS Simulator Tools (Available in our setup!)
We have `mcp__ios-simulator__screenshot` and `mcp__ios-simulator__ui_view` tools that can:
- Take screenshots programmatically
- Describe UI accessibility tree
- Tap, swipe, type input
- Perfect for automated visual testing!

#### 4. XCUITest with Screenshots
- Use `XCUIScreenshot` API within UI tests
- Can extract screenshots from test results using `xcparse`
- Good for full UI flow testing

## Recommended Implementation

### Phase 1: Command-Line Visual Test Runner (Simplest)

Create a test script that:
1. Launches app in simulator via `simctl`
2. Uses nREPL to execute drawing commands
3. Takes screenshots via `simctl` or MCP tools
4. Compares against reference images using ImageMagick or custom tool

```bash
# Example test flow
xcrun simctl boot "iPad Pro 13-inch (M4)"
xcrun simctl launch booted com.vybe.DrawingMobile-JIT-Sim

# Execute drawing via nREPL
clj-nrepl-eval -p 5580 "(m/clear!)"
clj-nrepl-eval -p 5580 "(m/draw-line! 100 100 500 500)"

# Take screenshot
xcrun simctl io booted screenshot /tmp/test_line.png

# Compare with reference
compare -metric AE /tmp/test_line.png reference/test_line.png diff.png
```

### Phase 2: Clojure Test Framework

Create `test/vybe/app/drawing/visual_test.clj`:

```clojure
(ns vybe.app.drawing.visual-test
  (:require [clojure.test :refer :all]
            [vybe.app.drawing.metal :as m]
            [vybe.test.visual :as visual]))

(deftest undo-produces-identical-result
  (m/clear!)
  (let [before (visual/screenshot "before")]
    (m/draw-line! 100 100 500 500)
    (m/undo!)
    (let [after (visual/screenshot "after")]
      (is (visual/images-match? before after :precision 0.99)))))

(deftest brush-replay-is-deterministic
  (m/clear!)
  (m/set-brush-type! :crayon)
  (m/draw-line! 100 100 500 500)
  (let [original (visual/screenshot "original")]
    (m/undo!)
    (m/redo!)
    (let [replayed (visual/screenshot "replayed")]
      (is (visual/images-match? original replayed :precision 0.99)))))

(deftest frame-switch-preserves-undo
  (m/frame-goto! 0)
  (m/clear!)
  (m/draw-line! 100 100 500 500)
  (m/undo!)
  (let [frame0-undone (visual/screenshot "frame0-undone")]
    (m/frame-goto! 1)
    (m/frame-goto! 0)
    (let [frame0-after-switch (visual/screenshot "frame0-after-switch")]
      (is (visual/images-match? frame0-undone frame0-after-switch)))))
```

### Phase 3: Visual Test Support Library

Create `src/vybe/test/visual.clj`:

```clojure
(ns vybe.test.visual
  (:require [clojure.java.shell :refer [sh]]))

(defn screenshot
  "Take screenshot of current simulator state"
  [name]
  (let [path (str "/tmp/visual-test-" name "-" (System/currentTimeMillis) ".png")]
    (sh "xcrun" "simctl" "io" "booted" "screenshot" path)
    path))

(defn images-match?
  "Compare two images, return true if they match within precision"
  [path1 path2 & {:keys [precision] :or {precision 1.0}}]
  (let [result (sh "compare" "-metric" "AE" path1 path2 "/tmp/diff.png")
        diff-pixels (parse-long (:err result))]
    ;; AE = Absolute Error (number of different pixels)
    ;; For 2732x2048 canvas = 5,595,136 pixels
    ;; precision 0.99 = allow 55,951 different pixels
    (< diff-pixels (* (- 1 precision) 5595136))))

(defn save-reference
  "Save current screenshot as reference image"
  [test-name]
  (let [path (str "test/references/" test-name ".png")]
    (sh "xcrun" "simctl" "io" "booted" "screenshot" path)
    path))
```

## Implementation Steps

### Step 1: Add ImageMagick Dependency
```bash
brew install imagemagick
```

### Step 2: Create Test Directory Structure
```
test/
  vybe/
    app/
      drawing/
        visual_test.clj
  references/           # Reference screenshots
    undo_basic.png
    brush_crayon.png
    ...
```

### Step 3: Create Makefile Target
```makefile
test-visual-ios:
	@echo "Starting iOS simulator..."
	xcrun simctl boot "iPad Pro 13-inch (M4)" 2>/dev/null || true
	@echo "Running visual tests..."
	clj -M:test -m vybe.app.drawing.visual-test
```

### Step 4: CI Integration (Future)
- Run tests on every PR
- Store reference images in git LFS
- Generate visual diff reports

## Key Test Cases

| Test | Description | Validates |
|------|-------------|-----------|
| `undo-basic` | Draw, undo, compare to blank | Undo restores canvas |
| `undo-redo-cycle` | Draw, undo, redo, compare to original | Redo restores drawing |
| `brush-replay` | Draw with jitter, undo, redo, compare | Deterministic replay |
| `frame-switch-undo` | Undo in frame, switch away, switch back | Frame cache updated after undo |
| `multi-brush-undo` | Draw with brush A, change to B, draw, undo twice | Brush settings preserved |

## Precision Considerations

- **Canvas size**: 2732 x 2048 = 5,595,136 pixels
- **Recommended precision**: 99.9% (allow 5,595 pixel differences)
- **Why not 100%?**: Anti-aliasing, subpixel rendering, floating-point accumulation
- **For deterministic tests**: Use 99.99% precision (allow 559 pixels)

## Alternative: Using MCP iOS Simulator Tools

We already have MCP tools available:

```
mcp__ios-simulator__screenshot     - Take screenshot
mcp__ios-simulator__ui_tap         - Tap at coordinates
mcp__ios-simulator__ui_type        - Input text
mcp__ios-simulator__ui_swipe       - Swipe gesture
mcp__ios-simulator__ui_view        - Get compressed screenshot
mcp__ios-simulator__ui_describe_all - Get accessibility tree
```

These could be used to build an interactive test harness that:
1. Takes screenshots via `mcp__ios-simulator__screenshot`
2. Performs gestures to trigger undo/redo
3. Compares screenshots programmatically

## Files to Create/Modify

1. **NEW**: `test/vybe/app/drawing/visual_test.clj` - Visual test cases
2. **NEW**: `src/vybe/test/visual.clj` - Visual testing utilities
3. **NEW**: `test/references/` - Reference screenshots directory
4. **MODIFY**: `Makefile` - Add `test-visual-ios` target
5. **NEW**: `ai/20260111-visual-ios-testing-plan.md` - This document

## Sources

- [Swift Snapshot Testing (Point-Free)](https://github.com/pointfreeco/swift-snapshot-testing)
- [Snapshot Testing in iOS (BrowserStack)](https://www.browserstack.com/guide/snapshot-testing-ios)
- [Snapshot Testing in iOS (Bitrise Blog)](https://bitrise.io/blog/post/snapshot-testing-in-ios-testing-the-ui-and-beyond)
- [simctl Command Line (XCTEQ)](https://www.xcteq.co.uk/xcblog/simctl-control-ios-simulators-command-line/)
- [XCUIScreenshot (Apple Developer)](https://developer.apple.com/documentation/xctest/xcuiscreenshot)
- [Creating Automated Screenshots with XCUITest](https://blog.winsmith.de/english/ios/2020/04/14/xcuitest-screenshots.html)
- [Metal Apps in Simulator (Apple Developer)](https://developer.apple.com/documentation/metal/developing-metal-apps-that-run-in-simulator)

## Next Steps

1. [ ] Install ImageMagick: `brew install imagemagick`
2. [ ] Create test directory structure
3. [ ] Implement `vybe.test.visual` namespace
4. [ ] Write first visual test (undo-basic)
5. [ ] Generate reference screenshots
6. [ ] Add Makefile target
7. [ ] Run and validate tests
