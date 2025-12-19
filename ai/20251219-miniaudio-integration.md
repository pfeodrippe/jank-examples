# Miniaudio Integration Session

**Date**: 2024-12-19

## What was done

### 1. Added miniaudio as a git submodule
```bash
git submodule add https://github.com/mackron/miniaudio.git vendor/miniaudio
```

### 2. Created implementation file
Created `vendor/miniaudio/miniaudio_impl.c`:
```c
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
```

### 3. Updated Makefile
- Added compilation rule for `vendor/miniaudio/miniaudio.o`
- Added to `SDF_JIT_OBJS`
- Added to clean target

### 4. Updated run_sdf.sh
- Added `-Ivendor/miniaudio` include path for both macOS and Linux
- Added `vendor/miniaudio/miniaudio.o` to `OBJ_FILES`

### 5. Created jank bindings
Created `src/vybe/audio.jank` with pure jank C++ interop (no cpp/raw):

**Key API:**
```clojure
;; Initialize/shutdown
(audio/init!)
(audio/uninit!)
(audio/initialized?)

;; Simple fire-and-forget playback
(audio/play! "sound.wav")

;; Master volume control
(audio/set-volume! 0.5)
(audio/get-volume)

;; Managed sounds (for more control)
(def snd (audio/sound "music.mp3"))
(audio/sound-start! snd)
(audio/sound-stop! snd)
(audio/sound-set-volume! snd 0.8)
(audio/sound-set-pitch! snd 1.2)
(audio/sound-set-pan! snd -0.5)  ;; left
(audio/sound-set-looping! snd true)
(audio/sound-playing? snd)
(audio/sound-at-end? snd)
(audio/sound-seek! snd 0)  ;; PCM frames
(audio/sound-destroy! snd)

;; 3D spatialization
(audio/sound-set-position! snd x y z)
(audio/sound-set-spatialization! snd true)
(audio/set-listener-position! x y z)
(audio/set-listener-direction! x y z)
```

## Key patterns used

### Global engine state with defonce + cpp/new
```clojure
(defonce engine-ptr-box
  (cpp/box (cpp/new (cpp/type "ma_engine*") cpp/nullptr)))
```
This persists across REPL reloads because:
1. `defonce` prevents redefinition of the var
2. `cpp/new` allocates on GC heap (survives JIT recompilation)
3. `cpp/box` wraps the pointer for jank storage

### Macros for native return types
```clojure
;; Use defmacro with syntax-quote to inline native-returning code
(defmacro get-engine-ptr []
  `(cpp/unbox (cpp/type "ma_engine**") engine-ptr-box))

(defmacro get-engine []
  `(cpp/* (get-engine-ptr)))
```
Macros inline at call sites, avoiding the issue where jank functions
can only return `object_ref`.

### Null checks with cpp/!
```clojure
;; Use cpp/! for null checks (not cpp/== or cpp/!= with nullptr)
(cpp/! ptr)        ;; true if ptr is null
(not (cpp/! ptr))  ;; true if ptr is NOT null
```

### When blocks with native side effects
```clojure
;; Always end when blocks with nil to avoid type mismatch
(when (not (cpp/! engine))
  (ma/ma_engine_set_volume engine (cpp/float. volume))
  nil)  ;; <-- ensures both branches return object type
```

### Header require for C API
```clojure
["miniaudio/miniaudio.h" :as ma :scope ""]
```
Then call functions directly: `(ma/ma_engine_init ...)`

### Boxing/unboxing pointers
```clojure
;; Create and box
(cpp/box (cpp/new cpp/ma_sound))

;; Unbox for use
(cpp/unbox (cpp/type "ma_sound*") snd-box)
```

## Files modified/created
- `vendor/miniaudio/` - git submodule
- `vendor/vybe/miniaudio_impl.c` - implementation file (IMPORTANT: outside submodule so it's git-tracked!)
- `src/vybe/audio.jank` - jank bindings
- `Makefile` - compilation rules
- `bin/run_sdf.sh` - include paths and object files

## CI Fix: Submodule files not tracked
**Problem**: Originally placed `miniaudio_impl.c` inside `vendor/miniaudio/` (the submodule).
This caused CI failure because:
1. Submodules are cloned from their remote origin
2. Our custom `miniaudio_impl.c` wasn't in the miniaudio repo
3. Make couldn't find the file: `No rule to make target 'vendor/miniaudio/miniaudio_impl.c'`

**Solution**: Move implementation file outside the submodule to `vendor/vybe/`:
```c
// vendor/vybe/miniaudio_impl.c
#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio/miniaudio.h"  // relative path to submodule
```

Update Makefile:
```makefile
vendor/vybe/miniaudio.o: vendor/vybe/miniaudio_impl.c vendor/miniaudio/miniaudio.h
	$(CC) $(CFLAGS) -c $< -o $@

SDF_JIT_OBJS = ... vendor/vybe/miniaudio.o  # not vendor/miniaudio/miniaudio.o
```

**Lesson**: Never put custom files inside git submodules - they won't be tracked!

## What to do next

1. **Test the integration**: Run `make sdf` and try:
   ```clojure
   (require '[vybe.audio :as audio])
   (audio/init!)
   (audio/play! "path/to/sound.wav")
   ```

2. **Add audio to vybe.sdf**: Could add UI sounds, ambient audio, etc.

3. **Consider adding**:
   - Sound groups for organized mixing
   - Fade in/out helpers
   - Resource manager integration for async loading

## Learned

- miniaudio is a single-header library - just `#define MINIAUDIO_IMPLEMENTATION` in one .c file
- The high-level `ma_engine` API is very simple: init, play_sound, uninit
- For managed sounds, use `ma_sound_init_from_file` + `ma_sound_start/stop`
- jank's `defonce` + `cpp/new` pattern works well for persistent global state
- Always use pure jank C++ interop when possible - only use cpp/raw (in a .hpp) if absolutely necessary
