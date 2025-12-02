# Learning: cpp/ Prefix API and Enum Constants

**Date**: 2024-12-02

## What I Learned

### 1. Enum/Constant Values are NOT Callable

When accessing C/C++ enum values via header requires, use them as bare symbols (no parentheses):

```clojure
;; WRONG - "This value is not callable"
(rl/SetConfigFlags (bit-or (rl/FLAG_WINDOW_RESIZABLE) (rl/FLAG_MSAA_4X_HINT)))

;; CORRECT - bare symbols
(rl/SetConfigFlags (bit-or rl/FLAG_WINDOW_RESIZABLE rl/FLAG_MSAA_4X_HINT))
(rl/IsKeyPressed rl/KEY_SPACE)
(rl/IsMouseButtonDown rl/MOUSE_BUTTON_LEFT)
```

### 2. Void Return Handling with let*

Use `_` binding in `let*` to discard void returns (avoids `option<void>` template errors):

```clojure
(let* [_ (cpp/some_void_function arg1 arg2)
       _ (cpp/native_add_view_offset dx dy)]
  :success)
```

### 3. Struct Field Access Uses Dash

Access struct fields with `cpp/.-field` (dash before field name):

```clojure
(let* [delta (rl/GetMouseDelta)]
  (cpp/.-x delta)   ;; delta.x
  (cpp/.-y delta))  ;; delta.y
```

### 4. Full cpp/ API from jank Codebase

Discovered from searching `/Users/pfeodrippe/dev/jank/compiler+runtime/test/cpp`:

| Pattern | C++ Equivalent | Example |
|---------|---------------|---------|
| `(cpp/int. 5)` | `int x = 5` | Type constructor |
| `(cpp/.-x obj)` | `obj.x` | Field access |
| `(cpp/.method obj)` | `obj.method()` | Method call |
| `(cpp/& x)` | `&x` | Address-of |
| `(cpp/* ptr)` | `*ptr` | Dereference |
| `(cpp/new type)` | `new type` | Allocate |
| `(cpp/delete ptr)` | `delete ptr` | Deallocate |
| `(cpp/== a b)` | `a == b` | Comparison |
| `(cpp/box ptr)` | wrap for jank | Opaque box |

## Commands I Ran

```bash
# Test integrated demo with pure jank input handling
./run_integrated.sh 2>&1 &
sleep 8
```

## Files Modified

1. `src/my_integrated_demo.jank`:
   - Removed manual constant definitions
   - Now uses `rl/FLAG_WINDOW_RESIZABLE`, `rl/KEY_SPACE`, `rl/MOUSE_BUTTON_LEFT` directly
   - Input handling uses pure jank with `let*` and `_` bindings

2. `ai/20251202-native-resources-guide.md`:
   - Updated enum section to show correct usage (no parentheses)
   - Updated input handling example

## Key Insight

Header require enums/constants are values, not functions. Access them like `rl/KEY_SPACE`, not `(rl/KEY_SPACE)`.
