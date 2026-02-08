# Fiction WASM Build - Success!

## Date: 2026-02-07

## What Was Fixed

### 1. re_pattern Codegen Bug in jank

**File**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` (line ~257)

**Problem**: The codegen for `re_pattern` objects was generating:
```cpp
jank::runtime::make_box<jank::runtime::obj::re_pattern>("pattern")
```

But `jank::runtime::obj::re_pattern` is not directly constructible. The `re_pattern` function in `jank::runtime::core.hpp` should be used instead.

**Solution**: Changed to generate:
```cpp
jank::runtime::re_pattern(jank::runtime::make_box("pattern"))
```

**The fix** (line 258):
```cpp
R"(jank::runtime::re_pattern(jank::runtime::make_box("{}")))",
```

Note the raw string syntax: `R"(...)"`  - starts with `R"(` and ends with `)"`, the `))` inside is part of the generated code.

### 2. Commented out nrepl server (not supported in WASM)

**File**: `/Users/pfeodrippe/dev/something/src/fiction.jank`

```clojure
;; Commented out - not supported in WASM
#_[jank.nrepl-server.server :as server]

;; In -main:
#_(server/start-server {:port 5700})
```

## Commands Used

```bash
# Edit codegen in jank
# Rebuild jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime && ninja -C build

# Rebuild fiction-wasm (script auto-detects jank changed and regenerates)
cd /Users/pfeodrippe/dev/something && make fiction-wasm

# Test with Node.js
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm
node --experimental-vm-modules -e "import('./fiction.js').then(m => m.default())"
```

## Key Learning

The `emscripten-bundle` script correctly handles regeneration:
- Line 452-454: Checks if `native_jank` is newer than generated `.cpp` files
- Line 459: Removes old generated file before regenerating
- No manual cleanup needed when jank compiler is rebuilt!

## Node.js Test Output

```
[jank-wasm] jank WebAssembly Runtime (AOT)
[jank-wasm] Module: fiction
[jank-wasm] Calling jank_init...
[jank-wasm] AOT mode: using pre-compiled code
[jank-wasm] Loading clojure.core-native...
[jank-wasm] Core native loaded!
...
[jank-wasm] Loading dependency: fiction.parser...
[jank-wasm] Loading dependency: vybe.flecs...
[jank-wasm] Loading dependency: fiction.state...
[jank-wasm] Loading dependency: fiction.render...
[jank-wasm] Module loaded successfully!
[jank-wasm] Found -main, calling it...

============================================
   LA VOITURE - Interactive Fiction
============================================

Controls:
  1-9      = Select choice
  Mouse    = Hover/click choice
  Up/Down  = Scroll dialogue
  ESC      = Quit

[fiction-wasm] Canvas size: 0x0
```

**Note**: Canvas size 0x0 is expected in Node.js - WebGPU/canvas APIs need a browser environment.

## Next Steps

1. Test the WASM build in browser with WebGPU
2. Run: `cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm && python3 -m http.server 8888`
3. Open: `http://localhost:8888/fiction_canvas.html`

## Files Modified

| File | Change |
|------|--------|
| `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` | Fixed re_pattern codegen |
| `/Users/pfeodrippe/dev/something/src/fiction.jank` | Commented out nrepl (not WASM compatible) |
