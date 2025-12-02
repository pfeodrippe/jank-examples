# JoltPhysics Integration with jank

**Date:** 2025-12-01

## Summary

Added JoltPhysics (https://github.com/jrouwe/JoltPhysics) as a git submodule and created a jank example demonstrating static linking integration.

## Files Created/Modified

- `vendor/JoltPhysics/` - Git submodule
- `vendor/JoltPhysics/CMakeLists.txt` - Custom CMake config for Release build
- `vendor/JoltPhysics/distr/objs/` - 125 extracted object files
- `src/my_jolt_static.jank` - Main jank demo file
- `run_jolt.sh` - Script to run with all object files

## Build Process

1. Built JoltPhysics in Release mode with:
   - `USE_ASSERTS=OFF` - Disable debug assertions
   - `INTERPROCEDURAL_OPTIMIZATION=OFF` - Regular object files (no LTO)
   - `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF`
   - `PROFILER_IN_DEBUG_AND_RELEASE=OFF`

2. Extracted object files from `libJolt.a` using `ar -x`

3. Run with jank using `--obj` flags for each object file

## What Works

- **PhysicsSystem initialization** - Full physics system setup
- **Table-based layer interfaces** - Using `BroadPhaseLayerInterfaceTable`, `ObjectLayerPairFilterTable`, `ObjectVsBroadPhaseLayerFilterTable`
- **Physics Update()** - Can step the physics simulation
- **Opaque box return** - Physics world returned to jank as opaque handle

## Known Limitation: Body Creation Crashes

Creating bodies with shapes (e.g., `SphereShape`, `BoxShape`) crashes with a segfault at virtual method dispatch (address 0x17).

**Root Cause:** When JIT-compiled code creates shape objects using `new SphereShape(...)`, the vtable pointer is set by JIT code. When precompiled Jolt code (in `CreateAndAddBody`) tries to call virtual methods on these shapes, the vtable is incompatible, causing a crash.

**Evidence:**
- `PhysicsSystem::Init()` works (uses table-based interfaces from Jolt headers)
- `SphereShape` creation succeeds (object allocated)
- Crash happens in `CreateAndAddBody` when calling shape virtual methods

## Workaround Attempts

1. **Tried custom virtual classes** - Defined `JankBPLayerInterface`, etc. - Same vtable issue
2. **Tried table-based interfaces** - Works! These are header-only from Jolt
3. **Tried `ShapeSettings::Create()`** - Still crashes, shape vtable still wrong
4. **Tried Release build** - Fixed debug assertion issues but not vtable problem

## Potential Solutions

1. **AOT compilation** - Compile jank code ahead-of-time so vtables match
2. **C API wrapper** - Create C functions that don't use virtual methods
3. **JIT vtable resolution** - Investigate if jank can link JIT vtables to precompiled ones

## Code Structure

```cpp
// JoltWorld struct holds all physics state
struct JoltWorld {
    TempAllocatorImpl* temp_allocator;
    JobSystemSingleThreaded* job_system;
    BroadPhaseLayerInterfaceTable* broad_phase_layer_interface;
    ObjectLayerPairFilterTable* object_vs_object_layer_filter;
    PhysicsSystem* physics_system;
    BodyID floor_id;
};
```

## Running the Demo

```bash
./run_jolt.sh
```

Output shows successful physics system initialization and Update() step.

## Commands Used

```bash
# Add submodule
git submodule add https://github.com/jrouwe/JoltPhysics vendor/JoltPhysics

# Build Jolt in Release mode
cd vendor/JoltPhysics && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_ASSERTS=OFF -DINTERPROCEDURAL_OPTIMIZATION=OFF ..
make -j8

# Extract object files
cd vendor/JoltPhysics && ar -x build/libJolt.a && mv *.o distr/objs/

# Run demo
./run_jolt.sh

# Debug with lldb
bash /tmp/run_jolt_lldb.sh
```

## What I Learned

1. **JIT vs precompiled vtables don't mix** - When JIT code creates C++ objects with virtual methods, the vtable pointer points to JIT memory. Precompiled code expects vtables from the same compilation unit.

2. **Table-based interfaces work** - Jolt provides `BroadPhaseLayerInterfaceTable`, `ObjectLayerPairFilterTable` that are header-only and work with JIT.

3. **Release build required** - Debug builds have `JPH_DEBUG` which enables assertions that require an `AssertFailed` symbol.

4. **Individual .o files needed** - Merged relocatable objects (`ld -r`) don't work with jank JIT; individual .o files do.

5. **Emulated TLS needed** - Jolt uses thread_local which requires `__emutls_get_address` stub for JIT.

## What To Do Next

1. **Investigate AOT compilation** - See if jank's AOT mode resolves the vtable mismatch
2. **Create C API wrapper** - Write non-virtual C functions that wrap Jolt's body creation
3. **Check jank JIT internals** - See if there's a way to make JIT use precompiled vtables
4. **Consider Jolt's C bindings** - Check if jolt-physics-rs or similar has C bindings we could use
