# iOS JIT Debugging Session - January 3, 2026

## Summary

Debugging "expected real found nil" crashes in iOS JIT mode. Identified several JIT-specific issues where values that work in AOT mode become nil in JIT mode.

## Crash Location Identified

The crash occurs at:
```
[UI 4] fps= 150.063766
<crash: expected real found nil>
```

After printing fps, the next line is:
```clojure
(imgui/SetNextWindowPos (imgui-h/ImVec2. (cpp/float. 10.0) (cpp/float. 10.0))
                        imgui-h/ImGuiCond_FirstUseEver)
```

**Root Cause**: `imgui-h/ImGuiCond_FirstUseEver` (a C++ enum constant from header require) returns `nil` in JIT mode instead of its actual integer value.

## Issues Found

### 1. Header Constants Not Retained in JIT Mode
C++ constants from header requires (like `ImGuiCond_FirstUseEver`) are not properly retained/accessible in JIT mode. They work on the first frame but become nil after GC runs.

**Fix**: Cache constants in `def` at load time or use literal values:
```clojure
;; Either cache at load time:
(def ^:private imgui-cond-first-use 4)  ;; ImGuiCond_FirstUseEver = 4

;; Or use literal:
(imgui/SetNextWindowPos pos 4)  ;; 4 = ImGuiCond_FirstUseEver
```

### 2. String Literals GC'd in JIT Mode
String literals returned from functions get garbage collected between frames because JIT code doesn't properly root them.

**Evidence**:
```
Frame 1: got greeting= Hello from vybe.sdf.greeting!!
Frame 2: got greeting= vulkan_kim/hand_cigarette.comp  ;; corrupted!
```

**Fix**: Cache strings in `def`:
```clojure
(def ^:private greeting-str "Hello from vybe.sdf.greeting!!")
(defn get-greeting [] greeting-str)
```

### 3. Non-libspec Vectors in load-libs
JIT codegen sometimes produces spurious vectors like `[#'clojure.core/*ns* vybe.util]` that get passed to `load-libs`.

**Fix**: Added filter in `clojure.core/load-libs` to skip non-libspec vectors.

## Symbol Table for Stack Traces

Added symbol address logging to help decode JIT stack traces. After loading modules, entry function addresses are written to `/tmp/jank-jit-symbols.txt`:

```
# Format: START_ADDR END_ADDR SYMBOL_NAME
0x117e24000 0x117e25000 jank_load_vybe_sdf_ui$loading__
```

This helps decode the `???` entries in crash stack traces.

## Commands Used

```bash
# Copy modified jank files to iOS resources
cp src/vybe/sdf/ui.jank SdfViewerMobile/jank-resources/src/jank/vybe/sdf/ui.jank
cp src/vybe/sdf/greeting.jank SdfViewerMobile/jank-resources/src/jank/vybe/sdf/greeting.jank

# Restart compile server
pkill -f "compile-server"; make ios-compile-server-sim &

# Launch app with console output
xcrun simctl launch --console-pty booted com.vybe.SdfViewerMobile-JIT-Sim

# Rebuild jank (with expanded paths!)
cd /Users/pfeodrippe/dev/jank/compiler+runtime
SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang \
CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++ \
./bin/compile
```

## Next Steps

1. Fix `imgui-h/ImGuiCond_FirstUseEver` - cache the constant or use literal value 4
2. Audit other header require constants that might have the same issue
3. Continue adding debug prints to find any remaining nil sources
4. Consider a more systematic fix in jank's JIT codegen for constant retention

## Files Modified

- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/jank/clojure/core.jank` - Added non-libspec filter
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp` - Added symbol logging
- `/Users/pfeodrippe/dev/something/src/vybe/sdf/greeting.jank` - Cached strings in def
- `/Users/pfeodrippe/dev/something/src/vybe/sdf/ui.jank` - Added debug prints
