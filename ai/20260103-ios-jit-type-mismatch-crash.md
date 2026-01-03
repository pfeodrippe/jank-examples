# iOS JIT Type Mismatch Crash Analysis

**Date**: 2026-01-03
**Error**: `[EXPECT_OBJECT] type mismatch: got 224 (unknown) expected 2 (integer)`

## Error Context

The crash occurs AFTER successful initialization:
- GLB loads successfully (140712 vertices, 211068 triangles)
- Mesh preview pipeline initializes
- iOS viewer starts
- Then crash during frame rendering

## Error Analysis

```
[EXPECT_OBJECT] type mismatch: got 224 (unknown) expected 2 (integer) ptr=0x1422ddc00
Memory dump: e0 db 2d 42 1 0 0 0 0 1 0 0 0 0 0 0 ...
```

### Type Value 224 (0xE0)
- This is NOT a valid jank object type
- Valid types are typically 0-50 range (nil=0, boolean=1, integer=2, real=3, etc.)
- 224 suggests memory corruption or reading garbage memory

### Memory Dump Interpretation
- `e0 db 2d 42 01 00 00 00` - First 8 bytes look like a pointer (little endian: 0x012d dbe0)
- The memory is not a properly formed jank object

## Root Cause Hypothesis

This is likely caused by the **reverted nil-handling fixes** in vybe.sdf.ui.jank and vybe.sdf.ios.jank.

In the previous session, we had fixes like:
```clojure
;; BEFORE (crashes when field returns nil):
(cpp/.-Framerate io)

;; AFTER (safe):
(or (cpp/.-Framerate io) 0.0)
```

When I accidentally ran `git checkout` on those files, it reverted ALL changes including:
1. The `(or ... default)` nil-safety patterns
2. Debug println statements (which was what we wanted to remove)

## Files That Need Fixes Restored

### 1. src/vybe/sdf/ui.jank
The draw-debug-ui! function accesses many C++ fields that could be nil:
- `(cpp/.-Framerate io)` - needs `(or ... 0.0)`
- Various mesh stats that could be uninitialized

### 2. src/vybe/sdf/ios.jank
Camera sync functions that read from C++:
- `(sdfx/get_camera_distance)` - needs nil check
- `(sdfx/get_camera_angle_x)` etc.

## The Pattern to Restore

For any C++ field access or FFI call that returns a numeric value:
```clojure
;; Instead of:
(let [value (cpp/.-field obj)]
  (use-value value))

;; Use:
(let [value (or (cpp/.-field obj) default-value)]
  (use-value value))
```

For ImGui calls that require specific types:
```clojure
;; Instead of:
(imgui/Text #cpp "FPS: %.1f" fps)

;; Ensure fps is not nil:
(imgui/Text #cpp "FPS: %.1f" (or fps 0.0))
```

## Immediate Fix Plan

1. **Identify the specific crash location** by looking at what code runs after "Starting iOS viewer..."
   - The `draw` function in ios.jank is called in the main loop
   - `draw-debug-ui!` in ui.jank renders ImGui

2. **Restore nil-safety patterns** to ui.jank and ios.jank WITHOUT the debug println statements

3. **Key locations to fix**:
   - ui.jank line 77: `fps (cpp/.-Framerate io)` → `fps (or (cpp/.-Framerate io) 0.0)`
   - Any `sdfx/get_*` calls that return numbers
   - Any `cpp/.-field` accesses

## Commands to Apply Fix

After restoring the fixes:
```bash
make ios-jit-clean && make ios-jit-sim-run
```

## Why This Happens

In AOT mode, the C++ code is compiled statically and type checking happens at compile time.

In JIT mode:
1. jank generates C++ code dynamically
2. The generated code calls into pre-compiled clojure.core functions
3. When a nil value flows through where an integer is expected, the runtime does:
   - `expect_object<obj::integer>(value)`
   - This reads the type field from the object
   - If value is nil or garbage, the type field is garbage (224)
   - Crash!

## Memory Layout Context

A jank object typically has:
- bytes 0-3: type enum (should be 0-50 ish)
- bytes 4-7: padding/flags
- bytes 8+: object data

When we see type=224, the pointer is pointing to:
- A freed object
- Uninitialized memory
- The wrong location entirely (nil pointer + offset)
