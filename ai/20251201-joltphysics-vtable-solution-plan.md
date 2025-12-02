# JoltPhysics vtable Mismatch - SOLVED!

**Date:** 2025-12-01
**Status:** SOLVED

## Problem Summary

When integrating JoltPhysics with jank's C++ JIT, body creation crashed with a segfault at virtual method dispatch (address 0x17). The root cause was **vtable incompatibility between JIT-compiled and precompiled code**.

### Why It Happened

1. **JIT-compiled code** creates a `SphereShape` via `new SphereShape(radius)`
2. The object is allocated in JIT memory with a vtable pointer pointing to **JIT-generated vtable**
3. **Precompiled Jolt code** (`CreateAndAddBody`) tries to call virtual methods on the shape
4. The vtable pointer points to invalid memory from precompiled code's perspective
5. **Crash** at virtual method dispatch

## Solution: Precompiled C++ Wrapper

Instead of using external C bindings (JoltC), we created our own **precompiled C++ wrapper** (`vendor/jolt_wrapper.cpp`) that:

1. Is compiled alongside Jolt's object files (vtables match)
2. Exposes `extern "C"` functions for JIT code to call
3. Creates all shapes/bodies inside precompiled code (correct vtables)
4. Returns opaque handles to JIT code

### Architecture

```
┌─────────────────────────────┐     ┌─────────────────────────────────────────┐
│   JIT CODE (my_jolt.jank)   │     │   PRECOMPILED (jolt_wrapper.o + Jolt)   │
├─────────────────────────────┤     ├─────────────────────────────────────────┤
│                             │     │                                         │
│ extern "C" {                │     │ extern "C" {                            │
│   uint32_t jolt_body_       │     │   uint32_t jolt_body_create_sphere(...) │
│     create_sphere(...);     │     │   {                                     │
│ }                           │     │     // Shape created HERE - vtable OK!  │
│                             │     │     new SphereShape(radius);            │
│ // Call precompiled fn      │     │     body_interface.CreateAndAddBody();  │
│ jolt_body_create_sphere();  │ --> │   }                                     │
│                             │     │ }                                       │
└─────────────────────────────┘     └─────────────────────────────────────────┘
```

## Files Created/Modified

### New Files

1. **`vendor/jolt_wrapper.cpp`** - Precompiled C++ wrapper with `extern "C"` functions
   - `jolt_world_create()` / `jolt_world_destroy()`
   - `jolt_body_create_sphere()` / `jolt_body_create_box()`
   - `jolt_body_get_position()` / `jolt_body_set_velocity()`
   - `jolt_world_step()` / `jolt_world_optimize_broad_phase()`

2. **`build_jolt.sh`** - Build script that compiles `jolt_wrapper.o`

### Modified Files

1. **`src/my_jolt_static.jank`** - Uses precompiled wrapper functions
2. **`run_jolt.sh`** - Includes `jolt_wrapper.o` in `--obj` flags

## Build & Run

```bash
# Build the wrapper
./build_jolt.sh

# Run the demo
./run_jolt.sh
```

## Test Output (SUCCESS!)

```
JoltPhysics Demo - Using Precompiled Wrapper
=============================================

Creating physics world...
World created: #object [opaque_box 0x105a4b1e0]

Creating floor (static box at y=0)...
Floor created, body ID: 8388608

Creating sphere (dynamic, at y=10)...
Sphere created, body ID: 8388609
Initial position: [0.000000 10.000000 0.000000]
Initial velocity: [0.000000 0.000000 0.000000]
Is active: true

Running physics simulation (60 steps at 1/60s)...

Step 0: pos=[0.000000 9.997277 0.000000] vel=[0.000000 -0.163364 0.000000]
Step 10: pos=[0.000000 9.820799 0.000000] vel=[0.000000 -1.789532 0.000000]
Step 20: pos=[0.000000 9.374531 0.000000] vel=[0.000000 -3.402201 0.000000]
Step 30: pos=[0.000000 8.660713 0.000000] vel=[0.000000 -5.001481 0.000000]
Step 40: pos=[0.000000 7.681568 0.000000] vel=[0.000000 -6.587481 0.000000]
Step 50: pos=[0.000000 6.439298 0.000000] vel=[0.000000 -8.160315 0.000000]

Final position: [0.000000 5.098089 0.000000]
Final velocity: [0.000000 -9.564696 0.000000]
Is active: true

Total bodies: 2
Active bodies: 1

Demo complete!
```

The sphere falls from y=10 to y=5.098 in 1 second, with velocity increasing to -9.56 m/s (gravity!).

## Key Insights

1. **JIT and precompiled C++ vtables are incompatible** - Objects with virtual methods created in JIT code cannot be used by precompiled code that calls their virtual methods.

2. **Solution: Precompiled wrapper with extern "C"** - By compiling a C++ wrapper alongside Jolt's object files, all vtables are consistent. JIT code calls `extern "C"` functions which don't use virtual dispatch.

3. **jank's --obj flag is the key** - Object files loaded via `--obj` are added to the JIT execution engine. Functions from these object files can be called from JIT code via `extern "C"` declarations.

4. **No need for external C bindings** - We don't need JoltC or similar projects. A simple custom wrapper is sufficient and stays up-to-date with Jolt.

5. **Pattern: Create in precompiled, use handles in JIT** - All object creation with virtual methods must happen in precompiled code. JIT code only passes handles (pointers) around.

## Commands Used

```bash
# Build Jolt in Release mode (already done)
cd vendor/JoltPhysics && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_ASSERTS=OFF -DINTERPROCEDURAL_OPTIMIZATION=OFF ..
make -j8

# Extract object files (already done)
cd vendor/JoltPhysics/distr/objs && ar -x ../../build/libJolt.a

# Compile wrapper
clang++ -c -O2 -std=c++17 -DNDEBUG -I vendor/JoltPhysics vendor/jolt_wrapper.cpp -o vendor/jolt_wrapper.o

# Run demo
./run_jolt.sh
```

## What's Next

1. Add more shape types (capsule, mesh, heightfield)
2. Add collision callbacks
3. Add constraints (hinges, sliders, etc.)
4. Integrate with Raylib for visualization
5. Consider making the wrapper more complete for real game use
