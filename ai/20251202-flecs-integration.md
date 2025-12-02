# Flecs Integration with jank JIT

**Date**: 2024-12-02

## What I Learned

### Critical: C++ Templates Cannot Be JIT-Resolved

When using Flecs C++ API like `world->each<Components...>(...)`, the compiler generates template-instantiated symbols that cannot be resolved by jank's JIT:

```cpp
// BAD: Template instantiation fails with JIT
world->each<PhysicsBody, RenderColor>([](flecs::entity, PhysicsBody& b, RenderColor& c) {
    // This generates symbols like __ZN5flecs5queryIJR8Position...
});
```

Error:
```
JIT session error: Symbols not found: [ __ZN5flecs... ]
error: no matching member function for call to 'each'
```

### Solution 1: Use Flecs C API Directly

The Flecs C API works because it's pure C without template magic:

```cpp
// GOOD: Use ecs_mini() and ecs_new() directly
static ecs_world_t* g_flecs_world = nullptr;

inline void flecs_init() {
    g_flecs_world = ecs_mini();  // C function, no templates
}

inline uint64_t flecs_create_entity() {
    return ecs_new(g_flecs_world);  // C function, works with JIT
}
```

Key C API functions:
- `ecs_mini()` - Create minimal Flecs world (faster, no addons)
- `ecs_init()` - Create full Flecs world with addons
- `ecs_fini(world)` - Destroy world
- `ecs_new(world)` - Create new entity
- `ecs_new_w_id(world, id)` - Create entity with component

### Solution 2: Use Vector for Iteration

Since `world->each<>()` template queries don't work, store entities in a vector for iteration:

```cpp
struct Entity {
    uint64_t flecs_id;  // Keep Flecs entity ID for reference
    uint32_t jolt_id;   // Other component data
    float radius;
    uint8_t r, g, b;
};

static std::vector<Entity>* g_entities_ptr = nullptr;

inline std::vector<Entity>& get_entities() {
    if (!g_entities_ptr) g_entities_ptr = new std::vector<Entity>();
    return *g_entities_ptr;
}

// Create entity in both Flecs and vector
inline void add_entity(int64_t jolt_id, double radius, int64_t r, int64_t g, int64_t b) {
    uint64_t flecs_id = ecs_new(g_flecs_world);  // Flecs entity
    Entity e = { flecs_id, (uint32_t)jolt_id, (float)radius, (uint8_t)r, (uint8_t)g, (uint8_t)b };
    get_entities().push_back(e);  // Store for iteration
}

// Iterate over vector (not Flecs query)
inline void draw_entities(void* jolt_world) {
    for (const Entity& e : get_entities()) {
        // Draw each entity...
    }
}
```

### Solution 3: Precompiled Wrapper (Use With Caution)

Even precompiled C++ wrappers can fail if they use Flecs C++ API internally:

```cpp
// flecs_jank_wrapper.cpp - CAUTION: This can still fail!
void* jank_flecs_create_world() {
    flecs::world* world = new flecs::world();  // C++ class with internal templates
    return static_cast<void*>(world);
}
```

The `flecs::world` C++ class constructor calls `ecs_init()` which works, but the class has internal C++ state that can cause issues.

**Better approach**: Use C API in the wrapper:

```cpp
void* jank_flecs_create_world_c() {
    return ecs_mini();  // Pure C, no C++ class issues
}
```

## What I Did

### 1. Integrated Flecs via C API

Changed from C++ wrapper to direct C API:
```cpp
static ecs_world_t* g_flecs_world_c = nullptr;

inline void flecs_init() {
    if (!g_flecs_world_c) {
        g_flecs_world_c = ecs_mini();
        printf("[flecs] ECS world created via ecs_mini()\n");
    }
}
```

### 2. Used Vector for Entity Storage

Entities are stored in both Flecs (for ID management) and a vector (for iteration):
```cpp
struct Entity {
    uint64_t flecs_id;  // Flecs entity ID
    uint32_t jolt_id;   // Jolt physics body ID
    float radius;
    uint8_t r, g, b;    // Render color
};

inline void flecs_add_entity(...) {
    uint64_t flecs_id = ecs_new(g_flecs_world_c);  // Create in Flecs
    get_entities().push_back({flecs_id, ...});     // Store in vector
}
```

### 3. Added Header Requires for Raylib/ImGui

Used jank's header require feature for cleaner C/C++ interop:
```clojure
(ns my-integrated-demo
  (:require
   ["raylib.h" :as rl :scope ""]
   ["imgui.h" :as imgui :scope "ImGui"]))

;; Call functions directly through header require
(defn imgui-new-frame! [] (imgui/NewFrame))
(defn raylib-shutdown! [] (rl/CloseWindow))
(defn begin-frame! []
  (rl/BeginDrawing)
  (rl/ClearBackground (rl/RAYWHITE)))
```

## Key Patterns for Flecs + jank

1. **Use Flecs C API** - `ecs_mini()`, `ecs_new()`, `ecs_fini()` work with JIT
2. **Avoid C++ templates** - `world->each<>()`, `world->component<>()` fail with JIT
3. **Store entities in vector** - For iteration, avoid Flecs query templates
4. **Keep Flecs entity IDs** - Store IDs for potential future C API queries
5. **Use header requires** - For direct C/C++ function calls from jank

## Commands Reference

```bash
# Run integrated demo
./run_integrated.sh

# Features:
# - 200 physics balls with horizontal velocity
# - Flecs ECS managing entity IDs
# - Vector-based entity iteration
# - ImGui debug panel
# - Pan/zoom/spawn controls
```

## File Structure

```
src/
└── my_integrated_demo.jank  # Integrated demo with Flecs C API

vendor/flecs/distr/
├── flecs.h                  # Flecs header (C and C++ API)
├── flecs.c                  # Flecs implementation
├── flecs.o                  # Precompiled object file
└── flecs_jank_wrapper.cpp   # C++ wrapper (use with caution)

ai/
├── 20251202-odr-fix-complete.md   # ODR fix documentation
└── 20251202-flecs-integration.md  # This file - Flecs integration
```
