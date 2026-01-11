# iOS Native Gesture Recognition for SDL App

## Research Summary

Based on research from [SDL3 Wiki](https://wiki.libsdl.org/SDL3/SDL_TouchFingerEvent), [Lazy Foo' tutorials](https://lazyfoo.net/tutorials/SDL/55_multitouch/index.php), and [Apple Developer Docs](https://developer.apple.com/documentation/uikit/uiswipegesturerecognizer):

### SDL3 Gesture Support
- **SDL_TouchFingerEvent**: Raw touch data (x, y, pressure, finger ID)
- **SDL_MultiGestureEvent**: Pinch/rotate detection (dDist, dTheta)
- **NO built-in swipe detection** - must implement manually or use native

### iOS Native Gesture Recognizers
- `UISwipeGestureRecognizer` - Fast, reliable swipe detection
- `UIPanGestureRecognizer` - Continuous drag tracking
- Built into iOS, tested by Apple, works with system gestures

## Problem
Manual swipe detection from SDL finger events is:
1. Unreliable - edge cases, timing issues
2. Complex - threshold tuning, direction detection
3. Conflicts with other gestures (pulley, two-finger)

## Solution: Hybrid Approach

Use **native iOS gesture recognizers** for swipe, keep SDL for:
- Pulley rotation (continuous tracking needed)
- Two-finger pan/zoom (SDL_MultiGestureEvent)
- Pencil drawing (SDL_EVENT_PEN_*)

### Implementation Plan

1. **Get SDL's UIView**
   ```objc
   SDL_PropertiesID props = SDL_GetWindowProperties(window);
   UIWindow* uiWindow = (__bridge UIWindow*)SDL_GetPointerProperty(
       props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
   UIView* view = uiWindow.rootViewController.view;
   ```

2. **Add UISwipeGestureRecognizers**
   ```objc
   UISwipeGestureRecognizer* swipeUp = [[UISwipeGestureRecognizer alloc]
       initWithTarget:handler action:@selector(handleSwipeUp:)];
   swipeUp.direction = UISwipeGestureRecognizerDirectionUp;
   swipeUp.numberOfTouchesRequired = 1;
   [view addGestureRecognizer:swipeUp];
   ```

3. **Communicate via atomic global**
   ```cpp
   static std::atomic<int> g_swipeDirection{0};  // -1=up, 0=none, 1=down
   ```

4. **Consume in event loop**
   ```cpp
   int swipe = g_swipeDirection.exchange(0);
   if (swipe == -1) frame_prev();
   else if (swipe == 1) frame_next();
   ```

5. **Handle gesture conflicts**
   - Set `gestureRecognizer.cancelsTouchesInView = NO` to allow SDL to still receive touches
   - Or use `gestureRecognizer.delaysTouchesEnded = NO`

### Key Consideration: Gesture Conflicts

UISwipeGestureRecognizer may conflict with:
- Pulley (long press + drag)
- Two-finger gestures

Solutions:
- Use `requireGestureRecognizerToFail:` for sequencing
- Or check `g_pulley.pending/active` before accepting swipe
- Or use UIPanGestureRecognizer with velocity check instead

### Alternative: UIPanGestureRecognizer with Velocity

More flexible than UISwipeGestureRecognizer:
```objc
- (void)handlePan:(UIPanGestureRecognizer *)pan {
    if (pan.state == UIGestureRecognizerStateEnded) {
        CGPoint velocity = [pan velocityInView:pan.view];
        if (fabs(velocity.y) > 500 && fabs(velocity.y) > fabs(velocity.x)) {
            // Fast vertical swipe detected
            g_swipeDirection = (velocity.y > 0) ? 1 : -1;
        }
    }
}
```

## Implementation (COMPLETED)

### Files Modified
- `DrawingMobile/drawing_mobile_ios.mm`:
  - Added `SwipeGestureHandler` class (lines 492-513)
  - Added `setupSwipeGestureRecognizers()` function (lines 517-549)
  - Called setup after SDL window creation (line 1887)
  - Added g_swipeDirection check in event loop (lines 2187-2201)
  - Removed manual swipe detection from pulley pending code
  - Added `cancelsTouchesInView = NO` and `delaysTouchesEnded = NO` to allow SDL to receive touches

### Key Implementation Details

1. **SwipeGestureHandler class**: ObjC class with `handleSwipeUp:` and `handleSwipeDown:` methods
2. **Global communication**: `static volatile int g_swipeDirection` set by gesture handlers
3. **Swipe consumption**: In event loop, only process swipes when `!g_pulley.active`
4. **Touch passthrough**: Both gesture recognizers have `cancelsTouchesInView = NO` so SDL still receives finger events

### Why Native Gestures Are Better
- iOS gesture recognizers are tested and reliable
- Handle edge cases (velocity thresholds, direction detection) automatically
- No threshold tuning required
- Consistent with iOS system behavior
- Simpler code - removed ~20 lines of manual swipe detection

## Commands
```bash
make drawing-ios-jit-sim-run
```

## What's Next
- Test on device to verify gesture detection works well
- Consider adding haptic feedback on frame change
- Monitor for any conflicts with pulley activation
