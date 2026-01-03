# iOS JIT Nil Constants - Root Cause Analysis

Date: 2026-01-04

## Problem Summary

- `make sdf-clean` (desktop JIT) - **WORKS**
- `make ios-aot-sim-run` (iOS AOT) - **WORKS**
- `make ios-jit-sim-run` (iOS JIT) - **FAILS** with "expected real found nil" or "not a number: nil"

The error occurs when accessing lifted constants (like `const_92717` = 0.0) that should have been initialized.

## What We've Tried and Failed

1. **Module loading order fix** - Moved `set_is_loaded` from Phase 1 to Phase 2
   - Result: Partially helped cross-module issues, but intra-module still fails

2. **Static prefix for constants** - Added `static` keyword to lifted vars/constants
   - Result: Made things WORSE (memory corruption instead of nil)

## Architecture Overview

### Compilation Targets

**eval target** (desktop REPL's compile_code):
- Constants declared as **struct members**
- Initialized in **constructor initializer list**
- Guaranteed to be initialized before any access

**module target** (iOS JIT's require_ns):
- Constants declared as **namespace-scope globals**
- Initialized via **placement-new** in `jank_load_XXX()`
- Temporal gap between declaration and initialization

### iOS JIT Loading Sequence

1. macOS compile server compiles code with `compilation_target::module`
2. Object file sent to iOS via base64 encoding
3. iOS loads object file into ORC JIT (`ee->addObjectFile()`)
4. iOS calls `jank_load_XXX()` which:
   a. Does placement-new on all globals (vars, constants)
   b. Calls `ns_load` which creates struct instances and binds vars
5. LATER: User code calls functions that access constants

## Generated Code Structure

```cpp
namespace vybe::sdf::ios {
    // Namespace-scope globals - UNINITIALIZED until jank_load runs!
    static jank::runtime::obj::real_ref const_92717;  // Initially nil

    struct update_uniforms_BANG__6 : jank::runtime::obj::jit_function {
        jank::runtime::object_ref call(jank::runtime::object_ref const dt) final {
            // Accesses const_92717 - what if it's still nil?
            auto const call_92716(jank::runtime::add(const_92717, dt));
            ...
        }
    };
}

extern "C" void jank_load_vybe_sdf_ios$loading__() {
    // 1. Initialize constants via placement-new
    new (&vybe::sdf::ios::const_92717) jank::runtime::obj::real_ref(
        jank::runtime::make_box<jank::runtime::obj::real>(0.0));

    // 2. Create structs and bind to vars
    vybe::sdf::ios::clojure_core_ns_load_92669_92670{ }.call();
}
```

## Key Observation

Desktop JIT also uses `compilation_target::module` for require/load operations!
Both use the same codegen path, but:
- Desktop uses CppInterOp/Clang's internal JIT
- iOS uses ORC JIT (LLVM's out-of-process JIT)

The difference is in how the JIT runtime handles the object file.

## Root Cause Hypothesis

### Hypothesis 1: ORC JIT Symbol Resolution Issue

When ORC JIT loads an object file with static namespace-scope variables:

1. Object file has BSS section for `const_92717`
2. Code has relocations pointing to `const_92717`
3. ORC JIT should allocate memory and patch relocations

**Possible issue**: ORC JIT might not properly handle static symbols within an object file, or might resolve them to external symbols with the same mangled name.

### Hypothesis 2: ARM64-Specific Relocation Issue

ARM64 uses different relocation types than x86_64:
- PC-relative addressing with limited offset range
- GOT (Global Offset Table) for certain data accesses
- ADRP/ADD instruction pairs for address calculation

**Possible issue**: The ARM64 object file might require GOT entries for static data, and ORC JIT might not properly set up the GOT on iOS.

### Hypothesis 3: Memory Section Handling

Object files have distinct sections:
- `.text` for code (executable, read-only)
- `.data` for initialized data (read-write)
- `.bss` for zero-initialized data (read-write)

**Possible issue**: ORC JIT might allocate `.bss` section but not properly link it to the code, or might allocate it with wrong permissions.

## Why Desktop JIT Works

Desktop JIT uses CppInterOp which wraps Clang's IncrementalCompiler:
- Same process as the compiler
- Direct symbol resolution via Clang's runtime
- No cross-process object file transfer
- Native x86_64 with simpler relocation model

## Why iOS AOT Works

iOS AOT compiles everything to static ARM64 object files:
- All modules linked together at build time
- Static linker resolves all symbols
- No runtime symbol resolution needed
- Initialization order guaranteed by link order

## Proposed Solutions

### Solution 1: Use eval-style initialization for iOS JIT

Modify the compile server to use `compilation_target::eval` for iOS JIT's require_ns.

**Pros**:
- Constants become struct members, guaranteed initialized
- No placement-new timing issues
- Already proven to work (desktop REPL uses this)

**Cons**:
- May have performance/memory implications (each struct instance has its own constants)
- Significant codegen change

### Solution 2: Debug logging to isolate the issue

Add debug logging to:
1. Print address of `const_92717` when placement-new runs
2. Print address of `const_92717` when accessed in struct code
3. If addresses differ, we've found the bug

**Implementation**:
```cpp
// In jank_load_XXX
std::cout << "[DEBUG] const_92717 at " << &vybe::sdf::ios::const_92717 << std::endl;

// In struct call method
std::cout << "[DEBUG] accessing const_92717 at " << &const_92717 << std::endl;
```

### Solution 3: Force symbol visibility

Ensure constants are not exported symbols that could conflict:
- Use anonymous namespace instead of static
- Use __attribute__((visibility("hidden")))
- Check ORC JIT symbol resolution settings

### Solution 4: Explicit initialization guards

Add an initialized flag that's checked before access:
```cpp
static std::atomic<bool> __constants_initialized{false};

// In jank_load
__constants_initialized = true;

// In struct access
assert(__constants_initialized && "Constants not initialized!");
```

## CRITICAL FINDING FROM AGENT INVESTIGATION

### Missing `initialize()` Call After Object File Loading!

**Location**: `processor.cpp` lines 445-448 and 472

```cpp
// File-based loading
llvm::cantFail(ee->addObjectFile(std::move(file.get())));
// NO initialize() call!

// Memory buffer loading
auto err = ee->addObjectFile(std::move(buffer));
// NO initialize() call!
```

**Compare with IR Module Loading** (line 495):
```cpp
llvm::cantFail(ee->addIRModule(jtl::move(m)));
llvm::cantFail(ee->initialize(ee->getMainJITDylib()));  // <-- THIS IS CALLED!
```

The comment at lines 445-446 explicitly states:
> "XXX: Object files won't be able to use global ctors until jank is on the ORC runtime"

**THIS IS LIKELY THE ROOT CAUSE!** Without `initialize()`, symbols may not be properly materialized, causing incorrect addresses for static namespace-scope variables.

### CodeModel Mismatch

- **JIT interpreter** uses `CodeModel::Large` (processor.cpp line 246)
- **persistent_compiler** (cross-compiles for iOS) uses DEFAULT CodeModel (persistent_compiler.hpp line 453)
- **AOT codegen** uses `CodeModel::Large` (context.cpp line 879)

This mismatch could cause ARM64 relocation issues!

## RECOMMENDED FIX

### Fix 1: Add initialize() call after addObjectFile (HIGHEST PRIORITY)

In `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/jit/processor.cpp`:

```cpp
// After line 448:
llvm::cantFail(ee->addObjectFile(std::move(file.get())));
llvm::cantFail(ee->initialize(ee->getMainJITDylib()));  // ADD THIS

// After line 472:
auto err = ee->addObjectFile(std::move(buffer));
if(err) { ... }
llvm::cantFail(ee->initialize(ee->getMainJITDylib()));  // ADD THIS
```

### Fix 2: Add explicit CodeModel to persistent_compiler

In `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/persistent_compiler.hpp`:

```cpp
// Around line 453, change:
target_machine_.reset(target->createTargetMachine(llvm::Triple(target_triple),
                                                  "generic",
                                                  "",
                                                  target_opts,
                                                  llvm::Reloc::PIC_));
// TO:
target_machine_.reset(target->createTargetMachine(llvm::Triple(target_triple),
                                                  "generic",
                                                  "",
                                                  target_opts,
                                                  llvm::Reloc::PIC_,
                                                  llvm::CodeModel::Large));  // ADD THIS
```

## Recommended Next Steps

1. **FIRST: Add initialize() call** after addObjectFile() in processor.cpp
2. **SECOND: Fix CodeModel** in persistent_compiler.hpp
3. **Rebuild jank** and test iOS JIT
4. If still failing, add debug logging to trace symbol addresses

## Files to Examine

- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/jit/processor.cpp` - ORC JIT setup
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/codegen/processor.cpp` - Code generation
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp` - iOS JIT loading
- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp` - Compile server

## Commands for Investigation

```bash
# Run iOS JIT with debug output
make ios-jit-sim-run 2>&1 | tee /tmp/ios_jit_debug.txt

# Check generated code
cat /tmp/jank-debug-vybe_sdf_ios.cpp | grep -A10 "const_92717"

# Check for symbol conflicts
nm /path/to/ios/app | grep const_92717
```
