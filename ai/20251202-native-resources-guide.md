# Native Resources Guide for jank

**Date**: 2024-12-02

This guide documents how to use native (C/C++) resources in jank, how to minimize `cpp/raw` usage, and what improvements could be made to jank to reduce wrapper boilerplate.

## Table of Contents

1. [Header Requires - The Clean Way](#header-requires---the-clean-way)
2. [The cpp/ Prefix - Direct C++ Access](#the-cpp-prefix---direct-c-access)
   - [Raw Array Access with cpp/aget](#raw-array-access-with-cppaget)
   - [Container Access with cpp/.at](#container-access-with-cppat)
   - [REPL-Persistent Mutable Primitives with cpp/new](#repl-persistent-mutable-primitives-with-cppnew)
   - [Boxing/Unboxing Typedef Pointer Types (Vulkan Handles)](#boxingunboxing-typedef-pointer-types-vulkan-handles)
   - [cpp/unbox Limitations](#cppunbox-limitations)
   - [void* Return Values Need Boxing](#void-return-values-need-boxing)
   - [cpp/cast Limitations with void*](#cppcast-limitations-with-void)
3. [Why Wrappers Are Still Needed](#why-wrappers-are-still-needed)
4. [Common Wrapper Patterns](#common-wrapper-patterns)
   - [Pattern 3: Return Struct Instead of Output Parameters](#pattern-3-return-struct-instead-of-output-parameters)
5. [What Works via Header Requires](#what-works-via-header-requires)
6. [What Doesn't Work (Requires cpp/raw)](#what-doesnt-work-requires-cppraw)
7. [C-Style Variadic Functions (WORKS!)](#c-style-variadic-functions-works)
8. [Potential jank Improvements](#potential-jank-improvements)
9. [Best Practices](#best-practices)
10. [C++ to jank Conversion Patterns](#c-to-jank-conversion-patterns)
11. [Vulkan Pipeline Patterns](#vulkan-pipeline-patterns)

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

### Raw Array Access with cpp/aget

Use `cpp/aget` for raw C array subscript access (`[]` operator):

```clojure
;; Define a raw C array
(cpp/raw "int numbers[] = {10, 20, 30, 40, 50};")

;; Access elements (subscript operator)
(cpp/aget cpp/numbers (cpp/int. 0))  ;; → 10
(cpp/aget cpp/numbers (cpp/int. 1))  ;; → 20
(cpp/aget cpp/numbers (cpp/int. 2))  ;; → 30

;; Modify elements (returns lvalue reference)
(cpp/= (cpp/aget cpp/numbers (cpp/int. 0)) (cpp/int. 99))
;; Now numbers[0] == 99

;; Works with pointers too
(cpp/raw "int* ptr = numbers;")
(cpp/aget cpp/ptr (cpp/int. 2))  ;; → 30
```

**Key points:**
- Works with raw C arrays and pointers (not just containers)
- First argument must be pointer or array type
- Second argument must be integral type (int, long, size_t, etc.)
- Returns an lvalue reference, allowing both reads and writes
- NOT bounds-checked (use `cpp/.at` for bounds-checked access)

### Container Access with cpp/.at

Use `cpp/.at` for bounds-checked container element access (the `.at()` method):

```clojure
;; Access vector element by index
(let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. idx))]
  (cpp/.-jolt_id entity)   ;; Access fields
  (cpp/.-radius entity))

;; Full example: iterate over vector and access struct fields
(dotimes [i (entity-count)]
  (let [entity (cpp/.at (cpp/get_entities) (cpp/size_t. i))
        jolt-id (cpp/.-jolt_id entity)
        radius (cpp/.-radius entity)
        r (cpp/.-r entity)]
    ;; Use the values...
    ))
```

**Key points:**
- Use `(cpp/size_t. idx)` to convert jank integer to C++ `size_t` (required by std::vector)
- Returns a reference to the element, allowing field access with `cpp/.-field`
- More efficient than wrapper functions since it avoids C++ call overhead
- Works with any container that has `.at()` method (std::vector, std::array, etc.)

**This pattern replaces C++ accessor helpers:**
```cpp
// OLD: C++ helper functions needed for each field
inline int64_t entity_jolt_id(int64_t idx) {
    return get_entities()[idx].jolt_id;
}
inline double entity_radius(int64_t idx) {
    return get_entities()[idx].radius;
}

// NEW: Direct access from jank - no wrappers needed!
// (cpp/.-jolt_id (cpp/.at (cpp/get_entities) (cpp/size_t. idx)))
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

### ⚠️ cpp/& with Mutable Reference Parameters

`cpp/&` works for some ImGui widgets but not all:

```clojure
;; WORKS: Checkbox with cpp/&
(imgui/Checkbox "Paused" (cpp/& (cpp/get_paused)))

;; DOES NOT WORK: Sliders cause JIT segfault
(imgui/SliderFloat "Scale" (cpp/& (cpp/get_scale)) 0.1 3.0)  ;; JIT crash
(imgui/SliderInt "Count" (cpp/& (cpp/get_count)) 1 20)       ;; JIT crash
```

**Workaround for Sliders:** Use C++ helper functions:

```cpp
// In cpp/raw block
inline bool imgui_slider_scale() {
    return ImGui::SliderFloat(\"Scale\", &get_scale(), 0.1f, 3.0f);
}
```

```clojure
(cpp/imgui_slider_scale)  ;; Use helper instead of cpp/&
```

**When cpp/& works:**
- `ImGui::Checkbox` with `bool*` parameter
- Taking address of local cpp variables: `(cpp/& (cpp/int. 5))`
- Pointer-to-pointer operations

**When cpp/& does NOT work:**
- `ImGui::SliderFloat`, `ImGui::SliderInt` (cause JIT segfault)
- Possibly other slider/drag widgets that take `float*` or `int*`

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

### REPL-Persistent Mutable Primitives with cpp/new

When you need mutable C++ primitives that **persist across REPL evaluations** (e.g., for ImGui widgets), use `cpp/new` instead of `cpp/bool.` or other stack-allocating constructors.

#### The Problem: Stack Allocation

```clojure
;; BAD: cpp/bool. creates stack-allocated temporary
(defonce is-paused (cpp/box (cpp/& (cpp/bool. false))))
;; ⚠️ Value is LOST when any REPL eval happens!
;; The stack is freed when the eval scope ends → dangling pointer
```

#### The Solution: GC Heap Allocation

```clojure
;; GOOD: cpp/new allocates on GC heap - persists across REPL evals!
(def is-paused (cpp/box (cpp/new cpp/bool false)))

;; Pass to ImGui Checkbox (needs raw pointer via unbox)
(imgui/Checkbox "Paused" (cpp/unbox cpp/bool* is-paused))

;; Read the current value (unbox + dereference)
(defn paused? [] (cpp/* (cpp/unbox cpp/bool* is-paused)))
```

#### Memory Allocation Comparison

| Construct | Allocation | Persistence | Use Case |
|-----------|------------|-------------|----------|
| `cpp/bool.` | Stack | Temporary - freed at scope end | Local temporaries in let* |
| `cpp/new cpp/bool` | GC heap (Boehm GC) | Persists until unreachable | Global mutable state |
| `cpp/box` | Wraps pointer in jank object | Required to store in vars | Storing any C++ pointer |

#### Pattern for ImGui Mutable Widgets

```clojure
;; Define mutable float (at load time)
(def my-float (cpp/box (cpp/new cpp/float 1.0)))

;; Pass to ImGui widget
(imgui/SliderFloat "Value" (cpp/unbox cpp/float* my-float) 0.0 10.0)

;; Read current value
(cpp/* (cpp/unbox cpp/float* my-float))

;; Define mutable int
(def spawn-count (cpp/box (cpp/new cpp/int 10)))
(imgui/SliderInt "Count" (cpp/unbox cpp/int* spawn-count) 1 100)
```

#### Why This Works

`cpp/new` allocates via `GC_malloc` (Boehm garbage collector):
- Memory survives REPL evaluations
- Automatically freed when pointer becomes unreachable
- Can be wrapped in `cpp/box` for storage in jank vars

### Void Return Handling

For side-effect-only calls, just call them sequentially in a function body - no need for `_` bindings:

```clojure
;; GOOD: Just call sequentially for pure side effects
(defn do-stuff! []
  (cpp/some_void_function arg1 arg2)
  (cpp/++ counter)
  (cpp/delete ptr)
  nil)

;; GOOD: Simple function - side effects in body
(let [foo (cpp/jank.some.foo.)]
  (cpp/++ foo)                    ;; Side effect - no binding needed
  (cpp/.-a foo))                  ;; Return value

;; GOOD: Complex function with sequential deps - use _ (do ...) to group
(let [cmd (alloc-cmd-buffer)
      _ (do (begin-cmd-buffer! cmd)
            (barrier-to-general! cmd image)
            (dispatch-compute! cmd))
      staging (create-staging-buffer size)]
  ...)

;; BAD: Don't use let* with multiple _ bindings
(let* [cmd (alloc-cmd-buffer)
       _ (begin-cmd-buffer! cmd)
       _ (barrier-to-general! cmd image)
       _ (dispatch-compute! cmd)]
  ...)
```

**Style guidelines:**
- Use `let` instead of `let*` - jank's `let` evaluates bindings sequentially
- Simple functions: call side-effects directly in the body
- Complex functions with sequential deps: use `_ (do ...)` to group side-effects

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

### Boxing/Unboxing Typedef Pointer Types (Vulkan Handles)

Vulkan types like `VkCommandBuffer`, `VkBuffer`, `VkImage` are **typedef pointers**:
```cpp
typedef VkCommandBuffer_T* VkCommandBuffer;  // VkCommandBuffer is already a pointer!
```

**Problem:** When using `cpp/type "VkCommandBuffer"`, jank adds an extra pointer level:
```clojure
;; WRONG: Creates VkCommandBuffer* which is VkCommandBuffer_T** (double pointer!)
(cpp/unbox (cpp/type "VkCommandBuffer") cmd)
```

**Solution:** Use the underlying struct pointer type:
```clojure
;; CORRECT: Creates VkCommandBuffer_T* (the actual handle type)
(cpp/unbox (cpp/type "struct VkCommandBuffer_T*") cmd)
```

**Complete pattern for Vulkan handles:**
```clojure
;; Boxing: wrap a Vulkan handle returned from C++
(defn alloc-cmd-buffer []
  (cpp/box (sdfx/alloc_screenshot_cmd)))  ;; alloc returns VkCommandBuffer

;; Unboxing: retrieve the handle to pass to Vulkan functions
(defn free-cmd-buffer! [cmd]
  (sdfx/free_screenshot_cmd
    (cpp/unbox (cpp/type "struct VkCommandBuffer_T*") cmd)))

;; All Vulkan handle types follow this pattern:
;; - VkCommandBuffer → "struct VkCommandBuffer_T*"
;; - VkBuffer        → "struct VkBuffer_T*"
;; - VkDeviceMemory  → "struct VkDeviceMemory_T*"
;; - VkImage         → "struct VkImage_T*"
;; - VkPipeline      → "struct VkPipeline_T*"
;; etc.
```

### cpp/unbox Limitations

**`cpp/unbox` only works for pointer types**, not value types:

```clojure
;; WORKS: Pointer types
(cpp/unbox (cpp/type "int*") boxed-ptr)
(cpp/unbox (cpp/type "struct VkBuffer_T*") boxed-handle)
(cpp/unbox (cpp/type "void*") boxed-void-ptr)

;; DOES NOT WORK: Value types (enums, ints, etc.)
(cpp/unbox (cpp/type "VkImageLayout") layout)  ;; Error!
(cpp/unbox (cpp/type "int") boxed-int)         ;; Error!
```

**Consequence:** When passing native enums/flags through jank function parameters, they get boxed and can't be unboxed. Solution: Keep enum/flag operations in C++ helpers, or use macros to inline the code.

### void* Return Values Need Boxing

Functions returning `void*` can't be auto-converted to jank objects:

```clojure
;; WRONG: void* can't auto-convert
(defn map-memory [memory size]
  (sdfx/map_screenshot_memory ...))  ;; Error: void* not convertible

;; CORRECT: Explicitly box the void*
(defn map-memory [memory size]
  (cpp/box (sdfx/map_screenshot_memory
             (cpp/unbox (cpp/type "struct VkDeviceMemory_T*") memory)
             (cpp/VkDeviceSize. size))))
```

### cpp/cast Limitations with void*

`cpp/cast` cannot cast from `void*` to other pointer types:

```clojure
;; DOES NOT WORK: Can't cast void* to uint8_t*
(cpp/cast (cpp/type "const uint8_t*") (cpp/unbox (cpp/type "void*") pixels))

;; WORKAROUND: Keep the cast in C++
;; In C++ helper:
inline int write_png(const void* pixelsVoid, ...) {
    const uint8_t* pixels = static_cast<const uint8_t*>(pixelsVoid);
    // ... process pixels ...
}
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

### Assignment Operator

Use `cpp/=` to assign values to C++ lvalues (references, dereferenced pointers):

```clojure
;; Assign to a reference returned by a function
(cpp/= (cpp/get_view_offset_x) (cpp/float. 640.0))  ;; get_view_offset_x() = 640.0f

;; Multiple assignments in a function - just call them sequentially
(defn reset-view! []
  (cpp/= (cpp/get_view_offset_x) (cpp/float. 640.0))
  (cpp/= (cpp/get_view_offset_y) (cpp/float. 500.0))
  (cpp/= (cpp/get_view_scale) (cpp/float. 15.0))
  nil)

;; Assign to a dereferenced pointer
(let* [ptr (cpp/new cpp/int 0)]
  (cpp/= (cpp/* ptr) (cpp/int. 42)))  ;; *ptr = 42
```

**Key points:**
- First argument must be an lvalue (reference or dereferenced pointer)
- Second argument is the value to assign
- Returns the assigned value (C++ assignment semantics)
- No need for `let*` with `_` binding - just call sequentially for side effects

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

### Pattern 3: Return Struct Instead of Output Parameters

For functions that need to return multiple values (especially Vulkan handles), return a struct instead of using output parameters. This avoids pointer-level issues with typedef pointer types.

**Problem: Output parameters with typedef pointers don't work**
```cpp
// C++ function with output params
inline bool create_buffer(VkDeviceSize size, VkBuffer* bufferOut, VkDeviceMemory* memoryOut);
```

```clojure
;; WRONG: cpp/new cpp/VkBuffer creates VkBuffer** (triple pointer!)
;; because VkBuffer is already VkBuffer_T*
(let* [buffer-ptr (cpp/new cpp/VkBuffer vk/VK_NULL_HANDLE)
       memory-ptr (cpp/new cpp/VkDeviceMemory vk/VK_NULL_HANDLE)
       success (sdfx/create_buffer size buffer-ptr memory-ptr)]  ;; Type error!
  ...)
```

**Solution: Return a struct**
```cpp
// Define a struct to hold multiple return values
struct BufferResult {
    VkBuffer buffer;
    VkDeviceMemory memory;
    bool success;
};

// Return the struct instead of using output params
inline BufferResult create_buffer(VkDeviceSize size) {
    BufferResult result{VK_NULL_HANDLE, VK_NULL_HANDLE, false};
    // ... create buffer and memory ...
    result.buffer = buffer;
    result.memory = memory;
    result.success = true;
    return result;
}
```

```clojure
;; CORRECT: Access struct fields directly
(defn create-staging-buffer [size]
  (let* [result (sdfx/create_buffer (cpp/VkDeviceSize. size))]
    (when (cpp/.-success result)
      {:buffer (cpp/box (cpp/.-buffer result))
       :memory (cpp/box (cpp/.-memory result))})))
```

**Key benefits:**
- Avoids pointer-level confusion with typedef pointer types
- Clean jank code using `cpp/.-field` access
- Works with Vulkan handles and other complex types

### Pattern 4: Vector Storage for Entity Iteration

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
| **Complex Pointer Math** | Can't express in jank | cpp/raw block |
| **Output Parameters** | cpp/new creates wrong pointer levels for typedef pointers | Return struct from C++ |
| **Callbacks** | Can't pass jank fn to C | C++ wrapper with trampoline |
| **Global State** | ODR violations | Heap-pointer pattern |
| **cpp/& with SliderFloat/Int** | JIT segfault (Checkbox works) | C++ wrapper functions |
| **Typedef pointer output params** | `cpp/new cpp/VkBuffer` creates VkBuffer** not VkBuffer* | Return struct from C++ |
| **Enum/flag function params** | Values get boxed, cpp/unbox only works for pointers | Keep enum ops in C++, or use macros |
| **void* to typed pointer cast** | cpp/cast doesn't work for void* source | Keep cast in C++ helper |
| **Large loops (>10k iterations)** | GC corruption with native type operations | Keep loop in C++ helper function |
| **jank's `slurp` with large files** | Segfaults on files >~1KB (bug in jank's slurp implementation) | Use C++ `read_text_file` helper or other file reading |

**Note:** C-style variadic functions (like `printf`, `ImGui::Text`) **DO work** - see next section.

---

## C-Style Variadic Functions (WORKS!)

C variadic functions using `...` (like `printf`, `snprintf`, `ImGui::Text`) work directly in jank. The codegen passes all arguments through to the C function call.

### Using #cpp Reader Macro (Recommended)

The `#cpp` reader macro provides the cleanest syntax for C++ string literals:

```clojure
;; ImGui::Text with format specifiers - clean and simple!
(imgui/Text #cpp "FPS: %d" (rl/GetFPS))

;; Multiple format arguments
(imgui/Text #cpp "%d + %d = %d" 1 2 3)

;; String format
(imgui/Text #cpp "Hello, %s!" #cpp "world")

;; printf works too
(cpp/printf #cpp "Value: %d\n" 42)
```

### Using cpp/value (Verbose Alternative)

```clojure
;; More explicit but verbose
(let* [fps (cpp/int (rl/GetFPS))]
  (cpp/ImGui.Text (cpp/value "\"FPS: %d\"") fps))

(cpp/printf (cpp/value "\"%d + %d = %d\\n\"") (cpp/int 1) (cpp/int 2) (cpp/int 3))
```

### Key Points

1. **No wrappers needed** - Call C variadic functions directly via header requires
2. **Use `#cpp` for string literals** - Cleaner than `cpp/value` with escaped quotes
3. **All arguments passed through** - jank doesn't need to know the function is variadic

### ⚠️ CRITICAL: Use Raw C Values, Not jank Wrappers

When passing values to `#cpp` format strings, you MUST use raw C values (from header require calls), NOT jank wrapper functions:

```clojure
;; BAD: jank wrapper returns boxed integer - prints garbage (memory address!)
(defn entity-count [] (cpp/.size (cpp/get_entities)))
(imgui/Text #cpp "Count: %d" (entity-count))  ;; Shows 7400053088 instead of 200!

;; GOOD: Call C function directly in the #cpp expression
(imgui/Text #cpp "Count: %d" (cpp/.size (cpp/get_entities)))  ;; Shows 200

;; BAD: jank wrapper
(defn num-active [w] (jolt/jolt_world_get_num_active_bodies (cpp/opaque_box_ptr w)))
(imgui/Text #cpp "Active: %d" (num-active w))  ;; Garbage!

;; GOOD: Call C API directly
(imgui/Text #cpp "Active: %d" (jolt/jolt_world_get_num_active_bodies (cpp/opaque_box_ptr w)))
```

**Why this happens:**
- jank wrapper functions (`defn`) return boxed jank objects (e.g., boxed int64)
- `#cpp` format strings expect raw C values
- Passing a boxed object prints its memory address, not its value
- Calling C functions directly (via header require) returns raw C values that work correctly

---

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

## C++ to jank Conversion Patterns

When converting C++ code to jank, these patterns are essential:

### 1. `bit-or` Returns jank Object - Cast for C++ Assignment

```clojure
;; WRONG: bit-or returns jank object, can't assign to C++ int
(cpp/= (cpp/.-ConfigFlags io) (bit-or flags new-flag))

;; CORRECT: Wrap with cpp/int to convert to C++ int
(cpp/= (cpp/.-ConfigFlags io)
       (cpp/int (bit-or (cpp/.-ConfigFlags io)
                        imgui-h/ImGuiConfigFlags_NavEnableKeyboard)))
```

### 2. `loop/recur` Doesn't Preserve Native Types

`loop/recur` converts native ints to jank objects on each iteration. For while loops with native ints, use `cpp/raw`:

```clojure
;; WRONG: loop/recur boxes the int, causing type errors
(loop [c (rl/GetCharPressed)]
  (when (> c 0)
    (cpp/.AddInputCharacter io c)
    (recur (rl/GetCharPressed))))  ;; Error: assigning jank object to int

;; CORRECT: Use cpp/raw for while loops with native ints
(cpp/raw "{ ImGuiIO& io = ImGui::GetIO(); int c = GetCharPressed(); while (c > 0) { io.AddInputCharacter(c); c = GetCharPressed(); } }")
```

### 2b. ⚠️ CRITICAL: Large Loops (>10k iterations) Corrupt GC

jank's GC **cannot handle large loops** (~10k+ iterations) with native type operations. Each iteration creates GC-managed objects for array indices, intermediate calculations, and loop variables. At high iteration counts, this corrupts the garbage collector:

```
EXC_BAD_ACCESS in GC_generic_malloc_many
EXC_BAD_ACCESS in GC_clear_fl_marks
```

**Example: Pixel processing (230k iterations)**

```clojure
;; CRASHES: 640×360 = 230,400 iterations corrupts GC
(dotimes [y out-h]
  (dotimes [x out-w]
    (let* [src-idx (cpp/size_t. (* (+ (* y width) x) 4))
           dst-idx (cpp/size_t. (* (+ (* y out-w) x) 3))]
      (cpp/= (cpp/aget rgb dst-idx) (cpp/aget src src-idx))
      ...)))

;; ALSO CRASHES: loop/recur has same problem
(loop [i (cpp/size_t. 0)]
  (when (< i total-pixels)
    ;; pixel operations...
    (recur (cpp/+ i (cpp/size_t. 1)))))
```

**Solution: Keep performance-critical loops in C++ header files**

```cpp
// In your .hpp file - not cpp/raw!
inline int write_png_downsampled(const char* filepath, const void* pixelsVoid,
                                  uint32_t width, uint32_t height, uint32_t scale) {
    const uint8_t* pixels = static_cast<const uint8_t*>(pixelsVoid);
    uint32_t out_w = width / scale;
    uint32_t out_h = height / scale;
    uint8_t* rgb = new uint8_t[out_w * out_h * 3];

    for (uint32_t y = 0; y < out_h; y++) {
        for (uint32_t x = 0; x < out_w; x++) {
            // pixel copy logic - ~230k iterations is fine in C++
        }
    }

    int result = stbi_write_png(filepath, out_w, out_h, 3, rgb, out_w * 3);
    delete[] rgb;
    return result;
}
```

```clojure
;; Call from jank - everything else can be jank!
(defn write-png-downsampled [filepath pixels width height scale]
  (sdfx/write_png_downsampled filepath
                              (cpp/unbox (cpp/type "void*") pixels)
                              (cpp/uint32_t. width)
                              (cpp/uint32_t. height)
                              (cpp/uint32_t. scale)))
```

**⚠️ WARNING: cpp/raw helpers also cause GC corruption!**

Even moving the loop to a `cpp/raw` inline function causes GC corruption after the function returns:

```clojure
;; STILL CRASHES later with GC corruption!
(cpp/raw "
inline void downsample_pixels(uint8_t* rgb, const uint8_t* src, ...) {
    for (...) { ... }  // Loop itself works
}
")
(cpp/downsample_pixels ...)  ;; GC corrupts AFTER this returns
```

**The ONLY reliable solution:** Keep loops in header files that are included via `:require`, not in `cpp/raw` blocks.

**Safe iteration counts:**
- < 1,000 iterations: Generally safe
- 1,000 - 10,000: May work, test thoroughly
- > 10,000 iterations: Use C++ helper in header file

### 3. Array Indexing with `->*`

```clojure
;; WRONG: Just gets &arr, not &arr[idx]
(let [idx (->* ib (aget offset))
      v (->* vb &)]  ;; This is &vb, not &vb[idx]!
  ...)

;; CORRECT: Use aget THEN & to get &arr[idx]
(let [idx (->* ib (aget offset))
      v (->* vb (aget idx) &)]  ;; This is &vb[idx]
  ...)
```

### 4. Struct Field Assignment

```clojure
;; C++: io.DisplaySize = ImVec2(width, height);
(cpp/= (cpp/.-DisplaySize io)
       (imgui-h/ImVec2. (cpp/float. (rl/GetScreenWidth))
                        (cpp/float. (rl/GetScreenHeight))))
```

### 5. Method Calls on References

```clojure
;; C++: io.AddMousePosEvent(x, y);
(cpp/.AddMousePosEvent io x y)

;; C++: io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
;; Complex out params - keep in C++ helper
```

### 6. Void Returns in `let*`

```clojure
;; Bind void-returning calls to _ in let*
(let* [_ (imgui/CreateContext)
       io (imgui/GetIO)
       _ (imgui/StyleColorsDark)
       _ (cpp/= (cpp/.-DisplaySize io) ...)]
  ...)
```

### 7. Out Parameters - Now Possible in Pure jank!

Out parameters CAN be handled in pure jank using `cpp/new`:

```clojure
;; C++: void GetData(unsigned char** pixels, int* w, int* h)

;; jank: allocate pointers on heap
(let [pixels_ptr (cpp/new (cpp/type "unsigned char*") (cpp/value "nullptr"))
      w_ptr (cpp/new cpp/int 0)
      h_ptr (cpp/new cpp/int 0)]
  ;; Pass pointers directly (they're already pointer-to-pointer)
  (cpp/.GetData obj pixels_ptr w_ptr h_ptr)
  ;; Dereference to read values
  (let [pixels (cpp/* pixels_ptr)
        w (cpp/* w_ptr)
        h (cpp/* h_ptr)]
    ...))
```

### 8. What to Keep in cpp/raw

Some things are still better in C++:
- **While loops with native types**: When loop variable must stay native (loop/recur boxes)

### 9. Header Require Scopes for Constants

Use two imports for namespaced C++ with constants:

```clojure
(:require
 ;; For functions: ImGui::Begin, ImGui::End
 ["imgui.h" :as imgui :scope "ImGui"]
 ;; For constants: ImGuiMod_Ctrl, ImGuiConfigFlags_NavEnableKeyboard
 ["imgui.h" :as imgui-h :scope ""])
```

---

## Summary

### What You CAN Do Without cpp/raw:
- Call C functions via header requires
- Call C++ namespaced functions
- Use enum values and constants
- Get simple return values (int, float, bool)
- **Call C variadic functions** like `printf`, `ImGui::Text` with `#cpp` literals
- **Box/unbox Vulkan handles** using `struct VkXxx_T*` types
- **Access struct fields** returned from C++ with `cpp/.-field`

### What REQUIRES cpp/raw or C++ Helpers:
- Opaque pointer handling (opaque_box)
- Type conversion (double ↔ float) for APIs that need float
- Global mutable state (ODR-safe pattern)
- C++ templates
- Complex iteration/rendering
- **Output parameters with typedef pointer types** (use struct return instead)
- Callbacks
- **Enum/flag operations passed through jank functions** (values get boxed)
- **void* to typed pointer casts** (cpp/cast doesn't work)
- **Large loops (>10k iterations)** - GC corruption; use C++ helper in header file (NOT cpp/raw!)

### Key Patterns for Vulkan/Graphics APIs:
1. **Boxing handles:** `(cpp/box (sdfx/get_handle))`
2. **Unboxing handles:** `(cpp/unbox (cpp/type "struct VkXxx_T*") boxed)`
3. **Struct returns:** Return struct from C++ instead of output params
4. **void* boxing:** `(cpp/box (sdfx/returns_void_ptr ...))`
5. **Keep in C++:** Enum ops, void* casts, complex barriers

---

## Vulkan Pipeline Patterns

### Struct Initialization with u/merge*

Instead of verbose field-by-field assignment, use the `u/merge*` macro from `vybe.util`:

```clojure
;; VERBOSE: Field-by-field assignment (avoid!)
(let [info (cpp/VkShaderModuleCreateInfo.)]
  (cpp/= (cpp/.-sType info) vk/VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO)
  (cpp/= (cpp/.-codeSize info) size)
  (cpp/= (cpp/.-pCode info) data)
  ...)

;; CLEAN: Using u/merge* macro
(let [info (cpp/VkShaderModuleCreateInfo.)]
  (u/merge* info
            {:sType vk/VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
             :codeSize size
             :pCode data})
  ...)
```

**Key points:**
- Use `cpp/VkType.` (value type with trailing dot) for stack allocation
- `u/merge*` expands to individual `cpp/=` assignments at compile time
- Works for any C struct type accessible via header requires
- String literals use `#cpp "string"` syntax

### Vulkan Create Functions (Output Parameters)

Vulkan `vkCreate*` functions use output parameters that don't work well in pure jank. Use small `cpp/raw` helpers that return the handle:

```clojure
;; Small helpers for Vulkan create functions (output params don't work in pure jank)
(cpp/raw "
inline VkShaderModule create_shader_module_jank(VkDevice device, VkShaderModuleCreateInfo* info) {
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(device, info, nullptr, &module);
    return module;
}
inline VkPipelineLayout create_pipeline_layout_jank(VkDevice device, VkPipelineLayoutCreateInfo* info) {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device, info, nullptr, &layout);
    return layout;
}
inline VkPipeline create_compute_pipeline_jank(VkDevice device, VkComputePipelineCreateInfo* info) {
    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, info, nullptr, &pipeline);
    return pipeline;
}
")

;; Usage in jank
(let [info (cpp/VkShaderModuleCreateInfo.)]
  (u/merge* info {:sType vk/VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
                  :codeSize size
                  :pCode data})
  (let [module (cpp/create_shader_module_jank device (cpp/& info))]
    ...))
```

### Vulkan Destroy Functions (Direct jank Calls)

Unlike create functions, Vulkan destroy functions work directly from jank:

```clojure
;; Direct Vulkan destroy calls - no wrappers needed!
(vk/vkDeviceWaitIdle device)
(vk/vkDestroyPipeline device pipeline cpp/nullptr)
(vk/vkDestroyPipelineLayout device layout cpp/nullptr)
(vk/vkDestroyShaderModule device module cpp/nullptr)
```

### Null Checks for Vulkan Handles

Use `cpp/!` for null checks instead of comparing with nullptr (avoids type mismatch):

```clojure
;; WRONG: Type mismatch between pointer and nullptr_t
(if (cpp/== module cpp/nullptr) ...)

;; CORRECT: Use cpp/! for null check
(if (cpp/! module)
  (println "Creation failed!")
  (println "Success!"))
```

### Future Improvements That Would Help:
1. Auto-wrap C pointers → Eliminate opaque_box boilerplate
2. Auto float conversion → Eliminate type cast wrappers
3. ODR-safe globals → Eliminate heap-pointer pattern
4. Template argument support → Enable some C++ template usage
5. Lambda support → Enable callbacks from jank
6. Output parameter support → Handle APIs with out params
7. **cpp/unbox for value types** → Enable unboxing enums/ints
8. **cpp/cast from void*** → Enable void* to typed pointer casts
9. **GC optimization for native loops** → Enable large loops without corruption
