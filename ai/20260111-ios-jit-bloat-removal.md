# iOS JIT Bloat Removal - Remove Unnecessary LLVM from iOS App

## Problem

The iOS JIT app bundles **235 MB of LLVM** (`libllvm_merged.a`) even though:
- All JIT compilation happens on the **Mac compile server**
- iOS only receives pre-compiled .o files via network
- iOS never uses LLVM - it just loads and executes compiled code

## Current Architecture

```
iOS App (with LLVM bundled - WRONG)     Mac Compile Server
┌─────────────────────────────────┐     ┌──────────────────────┐
│ libjank.a          587 MB       │     │ Full jank compiler   │
│ libllvm_merged.a   235 MB  ←UNUSED    │ LLVM/Clang           │
│ libjank_aot.a       15 MB       │     │ Cross-compile ARM64  │
│ libgc.a            596 KB       │     └──────────────────────┘
│ PCH files          104 MB       │              │
│ Headers             71 MB       │              │
└─────────────────────────────────┘              │
         ↑                                       │
         └───── receives .o files ───────────────┘
```

## Target Architecture

```
iOS App (minimal - CORRECT)             Mac Compile Server
┌─────────────────────────────────┐     ┌──────────────────────┐
│ libjank_runtime.a  ~100 MB      │     │ Full jank compiler   │
│ libjank_aot.a       15 MB       │     │ LLVM/Clang           │
│ libgc.a            596 KB       │     │ Cross-compile ARM64  │
│ Minimal headers     ~5 MB       │     └──────────────────────┘
└─────────────────────────────────┘              │
         ↑                                       │
         └───── receives .o files ───────────────┘
```

**Savings: ~400+ MB** (235 MB LLVM + 100 MB PCH/headers + potential runtime trimming)

## Implementation Plan

### Phase 1: Remove LLVM from iOS Linker Flags

**File**: `DrawingMobile/project-jit-sim.yml` and `DrawingMobile/project-jit-device.yml`

Remove from `OTHER_LDFLAGS`:
```yaml
# REMOVE THIS LINE:
- -lllvm_merged
```

### Phase 2: Build Minimal libjank for iOS

The current `libjank.a` (587 MB) includes compiler components. We need a **runtime-only** build.

**Option A**: Build jank with compile-time flag to exclude compiler
**Option B**: Create separate `libjank_runtime.a` that only has:
- Object model (persistent data structures)
- Garbage collector integration
- Native function calling
- Module loading (dlopen/dlsym for .o files)
- Network client for compile server

**NOT needed on iOS**:
- Parser/lexer
- Analyzer
- Code generator
- LLVM integration
- Incremental compiler

### Phase 3: Remove Unnecessary Headers/PCH

**Current bundled headers** (71 MB in `jank-resources/include/`):
- Full folly headers
- Full immer headers
- Full boost headers
- All jank compiler headers

**Minimal headers needed**:
- Runtime object types (`jank/runtime/object.hpp`)
- Native interop basics
- Core data structure interfaces

**PCH files** (104 MB):
- Can be completely removed if we pre-compile all native headers on Mac
- Or drastically reduced to only essential runtime headers

### Phase 4: Update Build Scripts

**File**: `Makefile` - iOS JIT targets

1. Don't copy `libllvm_merged.a` to iOS build
2. Don't sync full header tree
3. Build/use minimal PCH

**File**: `DrawingMobile/build_ios_jank_jit.sh`

Update to use runtime-only jank build.

## What iOS JIT Actually Needs

Looking at the compile server protocol, iOS only needs to:

1. **Send code to Mac** → Requires: network client (already in jank)
2. **Receive .o files** → Requires: base64 decoding
3. **Load .o into memory** → Requires: dlopen/dlsym (system calls)
4. **Execute jank code** → Requires: runtime object model + GC
5. **AOT core libs** → Already compiled: clojure.core, etc.

**Does NOT need**:
- LLVM (compilation happens on Mac)
- Full header tree (native compilation happens on Mac)
- Parser/lexer (parsing happens on Mac)
- Analyzer (analysis happens on Mac)
- Code generator (codegen happens on Mac)

## Files to Modify

1. `DrawingMobile/project-jit-sim.yml` - Remove `-lllvm_merged`
2. `DrawingMobile/project-jit-device.yml` - Remove `-lllvm_merged`
3. `Makefile` - Update iOS JIT build targets
4. `DrawingMobile/build_ios_jank_jit.sh` - Use minimal build
5. Potentially: jank compiler to add `JANK_RUNTIME_ONLY` build flag

## Quick Win: Just Remove LLVM Link - FAILED

Tried removing `-lllvm_merged` from linker flags. Result: **824 undefined symbols**.

### Offending Source Files (libjank.a)

| File | LLVM/Clang Refs | Purpose | Can Stub? |
|------|-----------------|---------|-----------|
| llvm_processor.cpp | 378 | LLVM IR codegen | YES - Mac does codegen |
| cpp_util.cpp | 106 | C++ type utilities | MAYBE |
| processor.cpp (jit/) | 140 | JIT processor | PARTIAL - need object loading |
| asio.cpp | 60 | nREPL server | PARTIAL - stub native headers |
| native_header_completion.cpp | 46 | Header completion | YES - Mac handles |
| evaluate.cpp | 18 | cpp/raw eval | MAYBE - remote eval |
| context.cpp | 36 | Runtime context | PARTIAL |
| clang_format.cpp | 5 | Code formatting | YES - Mac handles |

## Proper Solution: New CMake Option `jank_ios_jit_remote`

Need to add a new build mode to jank's CMakeLists.txt that:

1. **Excludes compiler components**:
   - `llvm_processor.cpp` - Not needed, Mac does codegen
   - `native_header_completion.cpp` - Mac handles
   - `clang_format.cpp` - Mac handles

2. **Stubs native header parsing in asio.cpp**:
   - Keep nREPL server
   - Stub out `describe_native_header_type`, `get_function_arg_type_string`, etc.
   - Route native header queries to compile server

3. **Keeps minimal JIT object loading**:
   - `jit/processor.cpp` but with stubbed compilation
   - Object loading via `load_object()` still needed
   - But this needs LLVM ORC runtime...

### The LLVM ORC Problem

Even with compilation on Mac, iOS still needs LLVM ORC to **load** the .o files:
```cpp
ee->addObjectFile(buffer);  // Needs LLVM ORC
ee->initialize(jitDylib);   // Needs LLVM ORC
```

**Options**:
1. **Keep minimal LLVM** - Just ORC runtime (~50MB instead of 235MB)
2. **Pre-link on Mac** - Send dylib instead of .o files (iOS dlopen restrictions)
3. **Use mach-o loader** - Custom loader (complex, fragile)

### Recommended Approach

**Phase 1**: Build jank with `jank_ios_jit_remote=ON` that:
- Excludes codegen (llvm_processor.cpp)
- Excludes native header completion
- Stubs cpp_util native functions
- Keeps ORC runtime for object loading (reduced LLVM)

**Phase 2**: Create minimal LLVM build:
- Only: ORC, Support, Object, BinaryFormat, MC
- Remove: CodeGen, Analysis, Transforms, InstCombine, etc.
- Target: ~50MB instead of 235MB

## Files to Modify in jank

### 1. CMakeLists.txt - Add new option

```cmake
option(jank_ios_jit_remote "iOS JIT via remote compile server (minimal LLVM)" OFF)

if(jank_ios_jit_remote)
  # Don't include compiler components
  # Use stubs for native header functions
  # Keep minimal ORC for object loading
endif()
```

### 2. Create stub files

- `src/cpp/jank/codegen/llvm_processor_stub.cpp`
- `src/cpp/jank/nrepl_server/native_header_stub.cpp`

### 3. Modify asio.cpp

Add `#ifdef JANK_IOS_JIT_REMOTE` guards around native header parsing functions.

## Commands to Test

```bash
# In jank compiler+runtime:
cd /Users/pfeodrippe/dev/jank/compiler+runtime

# Build with new remote JIT option (after adding to CMakeLists.txt):
./bin/build-ios build-ios-jit-remote Release simulator jit-remote

# Check library size:
ls -lh build-ios-jit-remote/libjank.a
```

## Risk Assessment

**Medium Risk**: Adding jank_ios_jit_remote option
- Requires careful separation of compiler vs runtime code
- Need to ensure remote compilation flow works correctly
- Stub functions must handle all code paths

**High Reward**:
- 235MB → ~50MB LLVM (minimal ORC only)
- Faster iOS builds
- Smaller app bundle

---

## CONCLUSION: Jank Not Needed for Production DrawingMobile

**Key Discovery**: `METAL_TEST_MODE = 1` is already set in `DrawingMobile/drawing_mobile_ios.mm`

This means the drawing app already runs **pure C++ SDL + Metal without using jank at runtime**.

Jank is only linked for:
- nREPL development workflow (hot reloading during dev)
- JIT compilation features

**For a production/release build**:
- Jank is NOT needed
- Could create `project-metal-only.yml` that only links:
  - SDL3 framework
  - Metal/UIKit/Foundation frameworks
  - imgui
  - Your C++ drawing code

**Estimated size reduction**: ~1GB → <50MB

**Decision**: JIT builds are for development only. Don't optimize JIT build size - instead create a separate minimal production configuration when needed for App Store release.
