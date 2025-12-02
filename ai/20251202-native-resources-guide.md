# Native Resources Guide for jank

**Date**: 2024-12-02

This guide documents how to use native (C/C++) resources in jank, how to minimize `cpp/raw` usage, and what improvements could be made to jank to reduce wrapper boilerplate.

## Table of Contents

1. [Header Requires - The Clean Way](#header-requires---the-clean-way)
2. [The cpp/ Prefix - Direct C++ Access](#the-cpp-prefix---direct-c-access)
3. [Why Wrappers Are Still Needed](#why-wrappers-are-still-needed)
4. [Common Wrapper Patterns](#common-wrapper-patterns)
5. [What Works via Header Requires](#what-works-via-header-requires)
6. [What Doesn't Work (Requires cpp/raw)](#what-doesnt-work-requires-cppraw)
7. [Potential jank Improvements](#potential-jank-improvements)
8. [Best Practices](#best-practices)

---

## Header Requires - The Clean Way

jank's header requires let you call C/C++ functions directly from jank without writing wrappers.

### Basic Syntax

```clojure
(ns my-namespace
  (:require
   ;; C functions (global scope)
   ["raylib.h" :as rl :scope ""]

   ;; C++ namespaced functions
   ["imgui.h" :as imgui :scope "ImGui"]))
```

### Scope Options

| Scope | Use Case | Example |
|-------|----------|---------|
| `:scope ""` | C functions, global C++ | `(rl/DrawText ...)` calls `DrawText()` |
| `:scope "Namespace"` | C++ namespace | `(imgui/Begin ...)` calls `ImGui::Begin()` |
| `:scope "Class"` | Static class methods | `(foo/Method ...)` calls `Class::Method()` |

### Selective Imports

```clojure
;; Only import specific symbols
["raylib.h" :as rl :scope "" :refer [DrawText DrawCircle]]

;; Rename symbols to avoid conflicts
["header.h" :as h :scope "" :rename {old_name new-name}]
```

### Working Examples from Our Codebase

```clojure
;; Raylib direct calls (no wrapper needed!)
(defn raylib-shutdown! [] (rl/CloseWindow))
(defn raylib-should-close? [] (rl/WindowShouldClose))
(defn get-dt [] (rl/GetFrameTime))

(defn begin-frame! []
  (rl/BeginDrawing)
  (rl/ClearBackground (rl/RAYWHITE)))

(defn end-frame! [] (rl/EndDrawing))

;; ImGui direct calls (no wrapper needed!)
(defn imgui-new-frame! [] (imgui/NewFrame))
(defn imgui-render! [] (imgui/Render))
```

---

## The cpp/ Prefix - Complete C++ Interop API

The `cpp/` prefix provides direct access to C++ constructs. Almost **everything** is accessible through `cpp/`.

### Type Constructors

Create C++ values with type constructors (note trailing dot):

```clojure
;; Builtin types
(cpp/int. 5)           ;; int x = 5
(cpp/float. 4.0)       ;; float x = 4.0
(cpp/bool. true)       ;; bool x = true
(cpp/double. 3.14)     ;; double x = 3.14

;; Structs/classes (namespace.Type.)
(cpp/test.Point. (cpp/int. 4) (cpp/int. 56))  ;; Point p{4, 56}

;; From real codebase:
(cpp/jank.cpp.member.pass_access.bar.)  ;; Create bar instance
```

### Struct Field Access

Use `cpp/.-field` (with dash) for field access:

```clojure
(let* [p (cpp/test.Point. (cpp/int. 4) (cpp/int. 56))]
  [(cpp/.-x p)    ;; Access p.x → 4
   (cpp/.-y p)])  ;; Access p.y → 56

;; Real example: Vector2 from raylib
(let* [delta (rl/GetMouseDelta)]
  (cpp/.-x delta)   ;; delta.x
  (cpp/.-y delta))  ;; delta.y
```

### Member Function Calls

Use `cpp/.method` (without dash) for method calls:

```clojure
(let* [bar (cpp/jank.cpp.call.member.pass_base.bar.)]
  (cpp/.get_a bar)   ;; bar.get_a()
  (cpp/.get_b bar))  ;; bar.get_b()

;; Method chaining
(cpp/.get (cpp/.inc (cpp/.add obj 5)))  ;; obj.add(5).inc().get()

;; File operations
(cpp/.open file path)
(cpp/.read file buffer size)
(cpp/.close file)
```

### Pointer Operations

```clojure
;; Reference (address-of): cpp/&
(let* [i (cpp/int. 5)
       &i (cpp/& i)]    ;; &i (pointer to i)
  ...)

;; Dereference: cpp/*
(let* [ptr (cpp/new cpp/int 42)]
  (cpp/* ptr))   ;; *ptr → 42

;; Pointer to pointer
(let* [&i (cpp/& i)
       &&i (cpp/& &i)]
  (cpp/* (cpp/* &&i)))  ;; **&&i
```

### Dynamic Memory: new/delete

```clojure
;; Allocate
(let* [ptr (cpp/new cpp/int 42)
       obj (cpp/new cpp/jank.some.Type)]
  ...)

;; Deallocate (void returns use _ binding)
(let* [ptr (cpp/new cpp/int 1)
       _ (cpp/delete ptr)]
  :success)
```

### Void Return Handling

**Key pattern**: Bind void-returning calls to `_` in `let*`:

```clojure
;; CORRECT: Use _ to discard void returns
(let* [_ (cpp/some_void_function arg1 arg2)
       _ (cpp/++ counter)
       _ (cpp/delete ptr)]
  :success)

;; Also works for side effects
(let* [foo (cpp/jank.some.foo.)
       _ (cpp/++ foo)
       _ (assert (= 6 (cpp/.-a foo)))]
  :success)
```

### Opaque Box: Wrapping C++ Pointers

```clojure
;; Box a pointer (wrap for jank)
(cpp/box (cpp/& (cpp/Position. (cpp/float. 4.0) (cpp/float. 2.0))))
(cpp/box (cpp/& (flecs/world.)))

;; Unbox to specific type
(let* [b (cpp/box ptr)
       typed-ptr (cpp/unbox cpp/int* b)]
  (cpp/* typed-ptr))  ;; Dereference the unboxed pointer
```

### Namespaced Access

Use dots for `::` in C++:

```clojure
;; namespace::subns::Type → cpp/namespace.subns.Type
cpp/jank.runtime.object_type.nil           ;; jank::runtime::object_type::nil
cpp/jank.cpp.enum_.pass_custom_size.foo.bar ;; enum value

;; Namespaced functions
(cpp/jank.runtime.object_type_str obj)     ;; jank::runtime::object_type_str(obj)
(cpp/jank.runtime.truthy o)                ;; jank::runtime::truthy(o)
```

### Type Casting

```clojure
;; Simple casts
(cpp/cast cpp/int (cpp/float. 4.0))    ;; (int)4.0f → 4
(cpp/cast cpp/double (cpp/float. 4.0)) ;; (double)4.0f → 4.0

;; String type casting
(cpp/cast cpp/std.string "meow")

;; Complex type via cpp/type
(cpp/cast (cpp/type "char const*") f)
```

### Value and Type Literals

```clojure
;; cpp/value - access C++ expressions directly
(cpp/value "std::numeric_limits<jtl::i64>::max()")
(cpp/value "&jank::cpp::member::meow<int>")

;; cpp/type - specify complex types
(cpp/type "char const*")
(cpp/type "jank::cpp::type::foo<int, long>")
```

### Comparison Operators

```clojure
;; Equality
(cpp/== a b)   ;; a == b
(cpp/!= a b)   ;; a != b

;; Comparison in conditions
(if (and (cpp/== bar (cpp/.-f1 meow))
         (cpp/!= bar spam))
  :success)
```

### Increment/Decrement

```clojure
;; Prefix increment/decrement
(let* [foo (cpp/some.foo.)
       _ (cpp/++ foo)                    ;; ++foo (void)
       result (cpp/++ foo)]              ;; ++foo (returns new value)
  result)

(let* [_ (cpp/-- counter)]  ;; --counter
  ...)
```

### Stream Operators

```clojure
;; Output stream
(let* [ss cpp/std.stringstream
       _ (cpp/<< ss "hello")]
  (cpp/.str ss))   ;; "hello"
```

### Raw C++ Code

```clojure
;; Define inline C++ functions
(cpp/raw "
int my_add(int x, int y) {
    return (x + y);
}

namespace test {
  struct Point {
    int x, y;
    Point(int x_val, int y_val) : x{x_val}, y{y_val} {}
  };
}
")

;; Call from jank
(cpp/my_add 4 33)  ;; → 37
```

### Complete Input Handling Example

```clojure
;; Use raylib enum constants directly (no def needed!)
(defn handle-input! []
  "Handle pan/zoom. Returns true if space pressed."
  (if (cpp/imgui_wants_input)
    false
    (let* [mouse-down (rl/IsMouseButtonDown rl/MOUSE_BUTTON_LEFT)]
      (when mouse-down
        (let* [delta (rl/GetMouseDelta)
               dx (cpp/.-x delta)
               dy (cpp/.-y delta)
               _ (cpp/native_add_view_offset dx dy)]
          nil))
      (let* [wheel (rl/GetMouseWheelMove)]
        (when (not= wheel 0.0)
          (let* [_ (cpp/native_scale_view (if (> wheel 0) 1.1 0.9))]
            nil)))
      (rl/IsKeyPressed rl/KEY_SPACE))))
```

---

## Why Wrappers Are Still Needed

Despite header requires, wrappers are needed for several reasons:

### 1. Opaque Pointer Handling (opaque_box)

jank needs to wrap C pointers as jank objects to pass them through Clojure code.

**Problem:**
```clojure
;; Can't do this - jank doesn't know how to handle void*
(def world (some-c-function-returning-pointer))
```

**Solution: opaque_box wrapper**
```cpp
inline jank::runtime::object_ref jolt_create_world() {
    void* world = jolt_world_create();
    return jank::runtime::make_box<jank::runtime::obj::opaque_box>(world, "JoltWorld");
}

inline void jolt_step(jank::runtime::object_ref w, double dt) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(w);
    jolt_world_step(o->data.data, (float)dt, 1);
}
```

### 2. Type Conversion (double ↔ float)

jank uses `double` for all floating-point numbers, but many C APIs use `float`.

**Problem:**
```clojure
;; jank passes double, but C function expects float
(c/set_position 1.5 2.0 3.0)  ; These are doubles!
```

**Solution: Conversion wrapper**
```cpp
inline void set_position_wrapper(double x, double y, double z) {
    c_set_position((float)x, (float)y, (float)z);  // Convert to float
}
```

### 3. Global State with ODR-Safe Pattern

Static variables in inline functions get duplicated when JIT recompiles, causing data loss.

**Problem:**
```cpp
// BAD: ODR violation - each JIT compilation gets its own copy!
static int g_counter = 0;

inline void increment() {
    g_counter++;  // Different g_counter in each compilation unit!
}
```

**Solution: Heap-pointer pattern**
```cpp
// GOOD: Single heap allocation survives JIT recompilation
static int* g_counter_ptr = nullptr;

inline int& get_counter() {
    if (!g_counter_ptr) g_counter_ptr = new int(0);
    return *g_counter_ptr;
}

inline void increment() {
    get_counter()++;  // Same counter across all compilations!
}
```

### 4. C++ Templates Don't Work with JIT

Template-instantiated symbols can't be resolved by jank's JIT linker.

**Problem:**
```cpp
// BAD: JIT error - symbols not found
world->each<Position, Velocity>([](auto& p, auto& v) { ... });
```

**Solution: Use C API or manual iteration**
```cpp
// GOOD: Use C API for Flecs
ecs_world_t* world = ecs_mini();
uint64_t entity = ecs_new(world);

// GOOD: Store in vector for iteration
static std::vector<Entity>* g_entities = nullptr;
inline std::vector<Entity>& get_entities() {
    if (!g_entities) g_entities = new std::vector<Entity>();
    return *g_entities;
}
```

### 5. Complex Rendering/Iteration Code

Some code is too complex for header requires (nested loops, callbacks, pointer math).

**Example: ImGui draw data rendering**
```cpp
// Too complex for header requires - needs cpp/raw
inline void imgui_render_draw_data() {
    ImDrawData* dd = ImGui::GetDrawData();
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList* cl = dd->CmdLists[n];
        // Complex pointer iteration, callbacks, etc.
    }
}
```

---

## Common Wrapper Patterns

### Pattern 1: Opaque Box for C Pointers

```cpp
#include <jank/runtime/obj/opaque_box.hpp>

// Create: wrap void* in opaque_box
inline jank::runtime::object_ref create_thing() {
    void* ptr = c_create_thing();
    return jank::runtime::make_box<jank::runtime::obj::opaque_box>(ptr, "ThingType");
}

// Use: extract void* from opaque_box
inline void use_thing(jank::runtime::object_ref thing_ref) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(thing_ref);
    void* ptr = o->data.data;
    c_use_thing(ptr);
}

// Destroy: extract and free
inline void destroy_thing(jank::runtime::object_ref thing_ref) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(thing_ref);
    c_destroy_thing(o->data.data);
}
```

### Pattern 2: ODR-Safe Global State

```cpp
// For each global variable, create heap-pointer accessor
static float* g_value_ptr = nullptr;

inline float& get_value() {
    if (!g_value_ptr) g_value_ptr = new float(1.0f);  // Default value
    return *g_value_ptr;
}

// Getter and setter wrappers
inline double native_get_value() { return get_value(); }
inline void native_set_value(double v) { get_value() = (float)v; }
```

### Pattern 3: Vector Storage for Entity Iteration

```cpp
struct Entity {
    uint64_t id;
    float x, y, z;
};

static std::vector<Entity>* g_entities_ptr = nullptr;

inline std::vector<Entity>& get_entities() {
    if (!g_entities_ptr) g_entities_ptr = new std::vector<Entity>();
    return *g_entities_ptr;
}

inline void add_entity(int64_t id, double x, double y, double z) {
    get_entities().push_back({(uint64_t)id, (float)x, (float)y, (float)z});
}

inline int64_t entity_count() {
    return (int64_t)get_entities().size();
}
```

---

## What Works via Header Requires

### Simple C Functions
```clojure
(rl/InitWindow 800 600 "Title")
(rl/CloseWindow)
(rl/BeginDrawing)
(rl/EndDrawing)
(rl/DrawText "Hello" 10 10 20 (rl/BLACK))
```

### C Functions Returning Simple Types
```clojure
(rl/GetFrameTime)      ; Returns float → double
(rl/GetFPS)            ; Returns int → int64
(rl/WindowShouldClose) ; Returns bool
(rl/GetScreenWidth)    ; Returns int → int64
```

### Enum Values / Constants (No Parentheses!)
```clojure
;; Access enum values as bare symbols (NOT callable!)
rl/RAYWHITE            ; Color constant
rl/DARKGRAY            ; Color constant
rl/KEY_SPACE           ; Key constant (32)
rl/FLAG_WINDOW_RESIZABLE ; Window flag
rl/MOUSE_BUTTON_LEFT   ; Mouse button (0)

;; Use in function calls:
(rl/SetConfigFlags (bit-or rl/FLAG_WINDOW_RESIZABLE rl/FLAG_MSAA_4X_HINT))
(rl/IsKeyPressed rl/KEY_SPACE)
(rl/IsMouseButtonDown rl/MOUSE_BUTTON_LEFT)
(rl/ClearBackground rl/RAYWHITE)
```

### C++ Namespace Functions
```clojure
(imgui/NewFrame)
(imgui/Render)
(imgui/Begin "Window Name")
(imgui/End)
(imgui/Text "Hello")
```

---

## What Doesn't Work (Requires cpp/raw)

| Feature | Why It Fails | Workaround |
|---------|--------------|------------|
| **C++ Templates** | JIT can't resolve instantiated symbols | Use C API or precompiled wrappers |
| **Lambdas/Closures** | Not wrapped in jank yet | Write C++ callback wrappers |
| **Static Member Access** | Blocked by Clang bug (LLVM #146956) | Wrapper function |
| **Variadic Functions** | Incomplete metadata | Fixed arity wrapper |
| **Complex Pointer Math** | Can't express in jank | cpp/raw block |
| **Output Parameters** | Can't pass pointers from jank | Wrapper that returns struct/vector |
| **Callbacks** | Can't pass jank fn to C | C++ wrapper with trampoline |
| **Global State** | ODR violations | Heap-pointer pattern |

### Example: Output Parameters

**Problem:**
```c
// C function with output parameters
void get_position(void* obj, float* x, float* y, float* z);
```

**Solution:**
```cpp
// Wrapper returns a jank vector
inline jank::runtime::object_ref get_position_wrapper(jank::runtime::object_ref obj) {
    auto o = jank::runtime::expect_object<jank::runtime::obj::opaque_box>(obj);
    float x, y, z;
    get_position(o->data.data, &x, &y, &z);
    return jank::runtime::make_box<jank::runtime::obj::persistent_vector>(
        std::vector<jank::runtime::object_ref>{
            jank::runtime::make_box((double)x),
            jank::runtime::make_box((double)y),
            jank::runtime::make_box((double)z)
        }
    );
}
```

---

## Potential jank Improvements

These are features that could reduce or eliminate the need for wrappers:

### 1. Auto-Wrap C Pointers

**Current:** Must manually wrap with opaque_box
```cpp
inline jank::runtime::object_ref create() {
    return jank::runtime::make_box<jank::runtime::obj::opaque_box>(ptr, "Type");
}
```

**Proposed:** jank auto-wraps pointer returns
```clojure
;; Just works - jank auto-wraps void* as opaque_box
(def world (c/create_world))
```

### 2. Automatic Float Conversion

**Current:** Must cast in wrapper
```cpp
inline void set_pos(double x, double y) {
    c_set_pos((float)x, (float)y);
}
```

**Proposed:** jank auto-converts double → float based on header signature
```clojure
;; Just works - jank sees float param, converts double
(c/set_pos 1.5 2.0)
```

### 3. ODR-Safe Globals

**Current:** Manual heap-pointer pattern
```cpp
static int* g_ptr = nullptr;
inline int& get() { if (!g_ptr) g_ptr = new int(0); return *g_ptr; }
```

**Proposed:** jank provides `cpp/defglobal` or similar
```clojure
(cpp/defglobal counter int 0)  ; ODR-safe by default
```

### 4. Template Argument Specification

**From jank TODO:** Auto-specify template arguments
```clojure
;; Proposed: specify template args
(std/vector<int>/push_back vec 42)
```

### 5. Output Parameter Support

**Proposed:** `^:out` metadata for output params
```clojure
;; Proposed: ^:out marks output parameters, returns tuple
(let [[x y z] (c/get_position ^:out ^:out ^:out obj)]
  ...)
```

### 6. Lambda/Callback Support

**From jank TODO:** Wrap jank functions as C++ lambdas
```clojure
;; Proposed: jank fn becomes C++ lambda
(c/for_each entities (fn [e] (println e)))
```

### 7. Static Member Access Fix

**Blocked by:** LLVM issue #146956 (Clang bug)
```clojure
;; Currently broken, should work when Clang fixes the bug
(Class/static_member)
```

---

## Best Practices

### 1. Prefer Header Requires Over cpp/raw

```clojure
;; GOOD: Use header require when possible
(defn close-window! [] (rl/CloseWindow))

;; AVOID: Unnecessary wrapper
(cpp/raw "inline void close_window() { CloseWindow(); }")
(defn close-window! [] (cpp/close_window))
```

### 2. Minimize cpp/raw Scope

```clojure
;; GOOD: Small, focused cpp/raw blocks
(cpp/raw "
inline void* create_thing() { return c_create(); }
inline void destroy_thing(void* t) { c_destroy(t); }
")

;; AVOID: Huge monolithic cpp/raw with everything
```

### 3. Use C APIs When Available

```clojure
;; GOOD: Flecs C API works with JIT
(cpp/raw "
inline void init_ecs() { ecs_mini(); }
")

;; AVOID: C++ templates fail with JIT
;; world->each<Pos, Vel>([](auto& p, auto& v) { ... });
```

### 4. Document Why Wrappers Exist

```cpp
// Wrapper needed because:
// 1. opaque_box for void* return
// 2. double→float conversion for x, y, z
inline jank::runtime::object_ref create_body(double x, double y, double z) {
    void* body = c_create_body((float)x, (float)y, (float)z);
    return jank::runtime::make_box<jank::runtime::obj::opaque_box>(body, "Body");
}
```

### 5. Use Consistent Naming

```cpp
// Pattern: native_* for ODR-safe state accessors
inline double native_get_value() { return get_value(); }
inline void native_set_value(double v) { get_value() = (float)v; }

// Pattern: jolt_* or flecs_* for library-specific wrappers
inline void jolt_step(...) { ... }
inline void flecs_init() { ... }
```

---

## Summary

### What You CAN Do Without cpp/raw:
- Call C functions via header requires
- Call C++ namespaced functions
- Use enum values and constants
- Get simple return values (int, float, bool)

### What REQUIRES cpp/raw:
- Opaque pointer handling (opaque_box)
- Type conversion (double ↔ float) for APIs that need float
- Global mutable state (ODR-safe pattern)
- C++ templates
- Complex iteration/rendering
- Output parameters
- Callbacks

### Future Improvements That Would Help:
1. Auto-wrap C pointers → Eliminate opaque_box boilerplate
2. Auto float conversion → Eliminate type cast wrappers
3. ODR-safe globals → Eliminate heap-pointer pattern
4. Template argument support → Enable some C++ template usage
5. Lambda support → Enable callbacks from jank
6. Output parameter support → Handle APIs with out params
