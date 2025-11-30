# Raylib Integration with Jank

This document describes how to integrate raylib with jank for native graphics applications.

## Quick Start - Exact Commands

These are the exact commands that were run to set up raylib with jank:

```bash
# 1. Clone raylib as a git submodule
git submodule add https://github.com/raysan5/raylib.git vendor/raylib

# 2. Create distribution directory
mkdir -p vendor/raylib/distr

# 3. Build raylib for native (macOS desktop)
cd vendor/raylib/src
make PLATFORM=PLATFORM_DESKTOP -j4

# 4. Copy native build artifacts to distr
cp vendor/raylib/src/libraylib.a vendor/raylib/distr/
cp vendor/raylib/src/*.o vendor/raylib/distr/
cp vendor/raylib/src/raylib.h vendor/raylib/src/raymath.h vendor/raylib/src/rlgl.h vendor/raylib/src/rcamera.h vendor/raylib/src/rgestures.h vendor/raylib/distr/

# 5. Build raylib for WASM
cd vendor/raylib/src
make clean
make PLATFORM=PLATFORM_WEB -j4

# 6. Copy WASM build artifacts (rename to avoid overwriting native)
cp vendor/raylib/src/libraylib.web.a vendor/raylib/distr/
cd vendor/raylib/distr
for f in *.o; do mv "$f" "${f%.o}_wasm.o"; done

# 7. Rebuild native and copy .o files again
cd vendor/raylib/src
make clean
make PLATFORM=PLATFORM_DESKTOP -j4
cp vendor/raylib/src/*.o vendor/raylib/distr/
cp vendor/raylib/src/libraylib.a vendor/raylib/distr/

# 8. Create the wrapper file (see content below)
# File: vendor/raylib/distr/raylib_jank_wrapper.cpp

# 9. Compile wrapper and create dynamic library
cd vendor/raylib/distr
clang++ -c raylib_jank_wrapper.cpp -o raylib_jank_wrapper.o -I. -std=c++17
clang++ -dynamiclib -o libraylib_jank.dylib \
  raylib_jank_wrapper.o \
  libraylib.a \
  -framework Cocoa \
  -framework IOKit \
  -framework OpenGL \
  -framework CoreVideo \
  -framework CoreFoundation \
  -std=c++17

# 10. Run the demo
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" \
jank -l/Users/pfeodrippe/dev/something/vendor/raylib/distr/libraylib_jank.dylib \
  --module-path src run-main my-raylib
```

## Overview

Raylib is a simple and easy-to-use library for videogames programming. Integrating it with jank requires a different approach than pure-C libraries like flecs due to raylib's platform-specific code (especially the GLFW backend on macOS which uses Objective-C and requires system frameworks).

## Setup

### 1. Clone raylib as a submodule

```bash
git submodule add https://github.com/raysan5/raylib.git vendor/raylib
```

### 2. Build raylib for native platform

```bash
cd vendor/raylib/src
make PLATFORM=PLATFORM_DESKTOP -j4
```

This creates `libraylib.a` and individual `.o` files in the `src` directory.

### 3. Build raylib for WASM (optional)

```bash
cd vendor/raylib/src
make clean
make PLATFORM=PLATFORM_WEB -j4
```

This creates `libraylib.web.a` for use with emscripten.

### 4. Create a distribution directory

```bash
mkdir -p vendor/raylib/distr
cp vendor/raylib/src/libraylib.a vendor/raylib/distr/
cp vendor/raylib/src/raylib.h vendor/raylib/distr/
cp vendor/raylib/src/raymath.h vendor/raylib/distr/
cp vendor/raylib/src/rlgl.h vendor/raylib/distr/
```

## Key Insight: Dynamic Library Approach

Unlike flecs (which compiles from a single `.c` file), raylib cannot be loaded directly as `.o` files into jank's JIT due to:

1. **Objective-C code**: The GLFW backend (`rglfw.c`) contains Objective-C code for macOS Cocoa integration
2. **Framework dependencies**: Requires linking against macOS frameworks (Cocoa, IOKit, OpenGL, CoreVideo, CoreFoundation)
3. **Symbol resolution**: The JIT cannot resolve Objective-C runtime symbols at load time

### Solution: Pre-compiled Dynamic Library

Create a wrapper that links raylib with all required frameworks into a dynamic library:

#### 1. Create wrapper file (`vendor/raylib/distr/raylib_jank_wrapper.cpp`)

```cpp
#include "raylib.h"

extern "C" {

void raylib_init_window(int w, int h, const char* title) {
    InitWindow(w, h, title);
}

void raylib_close_window() {
    CloseWindow();
}

void raylib_set_fps(int fps) {
    SetTargetFPS(fps);
}

bool raylib_should_close() {
    return WindowShouldClose();
}

void raylib_begin_drawing() {
    BeginDrawing();
}

void raylib_end_drawing() {
    EndDrawing();
}

void raylib_clear(int r, int g, int b, int a) {
    ClearBackground(Color{(unsigned char)r, (unsigned char)g,
                          (unsigned char)b, (unsigned char)a});
}

void raylib_draw_rect(int x, int y, int w, int h, int r, int g, int b, int a) {
    DrawRectangle(x, y, w, h, Color{(unsigned char)r, (unsigned char)g,
                                    (unsigned char)b, (unsigned char)a});
}

void raylib_draw_circle(int x, int y, float radius, int r, int g, int b, int a) {
    DrawCircle(x, y, radius, Color{(unsigned char)r, (unsigned char)g,
                                   (unsigned char)b, (unsigned char)a});
}

void raylib_draw_text(const char* text, int x, int y, int size, int r, int g, int b, int a) {
    DrawText(text, x, y, size, Color{(unsigned char)r, (unsigned char)g,
                                     (unsigned char)b, (unsigned char)a});
}

}
```

#### 2. Compile and link to dynamic library

```bash
cd vendor/raylib/distr

# Compile wrapper
clang++ -c raylib_jank_wrapper.cpp -o raylib_jank_wrapper.o -I. -std=c++17

# Create dynamic library with all frameworks
clang++ -dynamiclib -o libraylib_jank.dylib \
  raylib_jank_wrapper.o \
  libraylib.a \
  -framework Cocoa \
  -framework IOKit \
  -framework OpenGL \
  -framework CoreVideo \
  -framework CoreFoundation \
  -std=c++17
```

## Usage in Jank

### Example (`src/my_raylib.jank`)

```clojure
(ns my-raylib)

;; Extern declarations for wrapper functions
(cpp/raw "
extern \"C\" {
  void raylib_init_window(int w, int h, const char* title);
  void raylib_close_window();
  void raylib_set_fps(int fps);
  bool raylib_should_close();
  void raylib_begin_drawing();
  void raylib_end_drawing();
  void raylib_clear(int r, int g, int b, int a);
  void raylib_draw_rect(int x, int y, int w, int h, int r, int g, int b, int a);
  void raylib_draw_circle(int x, int y, float radius, int r, int g, int b, int a);
  void raylib_draw_text(const char* text, int x, int y, int size, int r, int g, int b, int a);
}
")

(defn init-window! [w h title]
  (cpp/raylib_init_window w h title))

(defn close-window! []
  (cpp/raylib_close_window))

;; ... more wrapper functions ...

(defn -main [& args]
  (init-window! 800 600 "jank + raylib demo")
  (set-fps! 60)

  (loop []
    (when-not (should-close?)
      (begin-drawing!)
      (clear! 245 245 245 255)
      (draw-rect! 100 100 200 150 230 41 55 255)
      (draw-circle! 500 200 80.0 0 228 48 255)  ;; Note: float for radius
      (draw-text! "Hello from jank!" 250 50 30 0 0 0 255)
      (end-drawing!)
      (recur)))

  (close-window!))
```

### Running

```bash
jank -l/path/to/vendor/raylib/distr/libraylib_jank.dylib --module-path src run-main my-raylib
```

## Important Notes

### Type Conversions
- Raylib's `Color` struct cannot be returned directly to jank (no automatic conversion)
- Use wrapper functions that take individual RGBA values as integers
- Float parameters (like circle radius) must use float literals (e.g., `80.0` not `80`)

### Directory Structure

```
vendor/raylib/
├── distr/
│   ├── raylib.h              # Main header
│   ├── raymath.h             # Math utilities
│   ├── rlgl.h                # OpenGL abstraction
│   ├── libraylib.a           # Native static library
│   ├── libraylib.web.a       # WASM static library
│   ├── libraylib_jank.dylib  # Pre-linked dynamic library for jank
│   ├── raylib_jank_wrapper.cpp
│   └── raylib_jank_wrapper.o
└── src/                      # Original raylib source
```

### Comparison with Flecs

| Aspect | Flecs | Raylib |
|--------|-------|--------|
| Source | Single `.c` file | Multiple `.c` files |
| Platform code | None | Objective-C (macOS) |
| Framework deps | None | Cocoa, IOKit, OpenGL, etc. |
| JIT loading | Direct `.o` file | Dynamic library required |
| Header include | `["flecs.h" :as flecs]` | Not directly (use extern "C") |

### Future Work

- Add more raylib function wrappers for textures, audio, input, etc.
- Create opaque_box wrappers for complex types (Camera, Texture2D, etc.)
- Test WASM build with emscripten
- Consider creating a jank library/namespace for common raylib operations
