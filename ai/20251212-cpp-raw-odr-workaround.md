# ODR Workaround: External C++ Headers and Object Files for cpp/raw

**Date:** 2025-12-12

## Problem

jank standalone builds (`jank compile -o name namespace`) caused ODR (One Definition Rule) violations because cpp/raw blocks are compiled twice:
1. JIT phase - compiled as `input_line_*` symbols during module loading
2. AOT phase - generated as C++ source for the final executable

Both end up in the same clang interpreter context, causing redefinition errors like:
```
error: redefinition of 'vybe_struct_begin'
input_line_189: note: previous definition is here
```

## Investigation Results

Various approaches were tried in `/Users/pfeodrippe/dev/jank/compiler+runtime/ODR_FIX_PLAN.md`:
- Skip JIT in evaluate.cpp - broke symbol resolution
- Skip AOT codegen - broke compilation (missing includes)
- Use `static inline` - still conflicts in same interpreter context
- Preprocessor guards - don't help across JIT/AOT parsing contexts

**Root cause**: jank uses the SAME clang interpreter for both JIT and AOT, so symbols conflict regardless of guards.

## Solution: External Header Files and Object Files

### Part 1: Pure C++ Functions → External Headers

For functions that don't use jank runtime types, move cpp/raw code to external header files in `vendor/vybe/`:

1. **`vendor/vybe/vybe_sdf_math.h`** - Math functions from `src/vybe/sdf/math.jank`
   - `sdf_sqrt`, `sdf_pow`, `sdf_sin`, `sdf_cos`, etc.
   - Pure C++ with `<cmath>` dependency

2. **`vendor/vybe/vybe_type_helpers.h`** - Type helpers from `src/vybe/type.jank`
   - `VybeStructBuilder` struct and functions
   - Field access helpers (`vybe_set_field_float`, etc.)
   - Direct memory access (`vybe_set_float_at`, etc.)
   - Standalone instance allocation

3. **`vendor/vybe/vybe_flecs_helpers.h`** - Pure Flecs helpers from `src/vybe/flecs.jank`
   - `vybe_is_system`, `vybe_entity_alive`, `vybe_entity_name`
   - Iterator helpers (`vybe_iter_count`, etc.)
   - Entity management (`vybe_delete_entity`, `vybe_remove_id`, etc.)

### Part 2: jank-Runtime-Dependent Functions → External Object File

For functions that use jank runtime types (`jank::runtime::object_ref`, `jank::runtime::make_box`, etc.),
create a separate compiled object file:

**`vendor/vybe/vybe_flecs_jank.cpp`** (compiled to `vybe_flecs_jank.o`):
- `vybe_system_callbacks` - static map for system callbacks
- `vybe_register_system_callback` / `vybe_unregister_system_callback`
- `vybe_system_dispatcher` - C callback that dispatches to jank
- `vybe_create_system` - creates Flecs system with dispatcher
- `vybe_create_entity_with_name` - uses `jank::runtime::to_string`
- `vybe_query_entities` / `vybe_query_entities_str` - return jank vectors
- `vybe_entity_ids` / `vybe_all_named_entities` / `vybe_children_ids` - return jank vectors

**Header file: `vendor/vybe/vybe_flecs_jank.h`** - Forward declarations for the functions

### Files Modified

1. **`src/vybe/sdf/math.jank`**
   - Removed cpp/raw block
   - Added: `["vybe/vybe_sdf_math.h" :as _ :scope ""]`

2. **`src/vybe/type.jank`**
   - Removed all cpp/raw blocks
   - Added: `["vybe/vybe_type_helpers.h" :as _ :scope ""]`

3. **`src/vybe/flecs.jank`**
   - Removed cpp/raw block entirely
   - Added: `["vybe/vybe_flecs_helpers.h" :as vfh :scope ""]`
   - Added: `["vybe/vybe_flecs_jank.h" :as vfj :scope ""]`

4. **`bin/run_sdf.sh`**
   - Added `-Ivendor` to include paths
   - Added compilation step for `vybe_flecs_jank.cpp`
   - Added `vendor/vybe/vybe_flecs_jank.o` to OBJ_FILES

5. **`bin/run_tests.sh`**
   - Added `vendor/vybe/vybe_flecs_jank.o` to object files

## jank Compiler Fix

Fixed a bug in jank's AOT processor where `--obj` files were being parsed as C++ source
due to the `-x c++` flag affecting subsequent arguments.

**File:** `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/aot/processor.cpp`

**Change:** Object files from `--obj` are now passed with `-Wl,` prefix to go directly
to the linker, avoiding the `-x c++` language override:
```cpp
/* Before: */
compiler_args.push_back(strdup(obj.c_str()));

/* After: */
compiler_args.push_back(strdup(util::format("-Wl,{}", obj).c_str()));
```

## Important Notes

1. **Header require syntax**: Must use `:as alias` (alias cannot be `_` for multiple headers in same ns):
   ```clojure
   ["vybe/header1.h" :as h1 :scope ""]
   ["vybe/header2.h" :as h2 :scope ""]
   ```

2. **Include paths**: `vendor/` is already in include path via `-I./vendor` in build scripts

3. **All headers use include guards** (`#ifndef VYBE_*_H`)

4. **Compilation of vybe_flecs_jank.o** requires jank headers:
   ```bash
   clang++ -c vendor/vybe/vybe_flecs_jank.cpp -o vendor/vybe/vybe_flecs_jank.o \
       -DIMMER_HAS_LIBGC=1 \
       -I$JANK_SRC/include/cpp \
       -I$JANK_SRC/third-party \
       -I$JANK_SRC/third-party/bdwgc/include \
       -I$JANK_SRC/third-party/immer \
       -I$JANK_SRC/third-party/bpptree/include \
       -I$JANK_SRC/third-party/folly \
       -I$JANK_SRC/third-party/boost-multiprecision/include \
       -I$JANK_SRC/third-party/boost-preprocessor/include \
       -I$JANK_SRC/build/llvm-install/usr/local/include \
       -Ivendor -Ivendor/flecs/distr \
       -std=c++20 -fPIC
   ```

## Verification

- `make sdf` (JIT mode) works - app starts, displays viewer, saves screenshot
- `make sdf-standalone` (AOT mode) works - builds app bundle successfully (278M)
- Both modes tested successfully

## Architecture

```
+------------------+      +----------------------+      +---------------------+
|   flecs.jank     | ---> | vybe_flecs_helpers.h | ---> | Pure flecs helpers |
| (no cpp/raw)     |      | (inline functions)   |      | (no jank deps)     |
+------------------+      +----------------------+      +---------------------+
        |
        v
+----------------------+      +---------------------+
| vybe_flecs_jank.h    | ---> | Forward declarations|
| (declarations only)  |      | for jank-dep funcs  |
+----------------------+      +---------------------+
        |
        v
+----------------------+      +---------------------+
| vybe_flecs_jank.cpp  | ---> | Implementation with |
| (compiled to .o)     |      | jank runtime types  |
+----------------------+      +---------------------+
```

The .o file is:
- Loaded via `--obj` during JIT for symbol resolution
- Linked via `--obj` during AOT for the final executable
