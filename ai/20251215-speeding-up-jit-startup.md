# Speeding Up `make sdf` JIT Startup

**Date**: 2025-12-15
**Goal**: Make `make sdf` start as fast as `make sdf-standalone`

## Problem Statement

`make sdf` is slow to start because it runs `clean-cache` first, which removes the `target/` directory containing cached compiled modules. This forces full JIT recompilation on every run.

`make sdf-standalone` starts fast because it pre-compiles everything into a single binary with an embedded PCH (precompiled header).

## Research Findings

### 1. jank Already Has Built-in Caching!

**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp`

The module loader already supports loading pre-compiled `.o` files:

```cpp
// Lines 754-796 in loader.cpp
if(entry.o.is_some() && entry.o.unwrap().archive_path.is_none() && entry.o.unwrap().exists()
   && (entry.jank.is_some() || entry.cljc.is_some() || entry.cpp.is_some()))
{
    // Check if .o file is NEWER than source
    if(std::filesystem::last_write_time(o_file_path).time_since_epoch().count()
       >= source_modified_time)
    {
        return find_result{ entry, module_type::o };  // Use cached .o!
    }
}
```

**Key insight**: If a `.o` file exists AND is newer than its source file, jank will load it directly without recompilation!

### 2. Binary Version Hash

**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/util/environment.cpp`

jank creates a unique `binary_version` hash based on:
- `JANK_VERSION`
- Clang revision
- JIT flags
- Optimization level
- Codegen mode
- Include directories

This creates a cache directory like: `target/arm64-apple-darwin24.1.0-abc123...`

Different configurations get different cache directories, so there's no risk of using stale caches from a different build configuration.

### 3. How Modules Get Cached

**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/context.cpp`

When `*compile-files*` is true (set during `compile` command), modules are written to:
```cpp
// Line 583
util::format("{}/{}.o", binary_cache_dir, module::module_to_path(module_name))
```

### 4. Why `clean-cache` Exists

From `Makefile`:
```makefile
# Run targets (clean cache first to avoid stale module issues)
sdf: clean-cache
	./bin/run_sdf.sh
```

This was likely added to avoid stale module issues, but jank's built-in caching handles this via:
1. Timestamp comparison (source newer than .o → recompile)
2. Binary version hash (different flags → different directory)

### 5. What Happens During AOT (`sdf-standalone`)

**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/aot/processor.cpp`

1. All modules are compiled to `.o` files in `target/<binary_version>/`
2. A main entrypoint is generated with embedded PCH
3. All `.o` files are linked into a single binary
4. Each module gets a `jank_load_<module>()` function

---

## Implementation Plan

### Option A: Simple Fix (Recommended for Quick Win)

**Remove `clean-cache` from `sdf` target**

```makefile
# Before
sdf: clean-cache
	./bin/run_sdf.sh

# After
sdf:
	./bin/run_sdf.sh
```

**Pros**:
- Immediate speedup on subsequent runs
- No jank changes required
- jank's built-in caching handles staleness

**Cons**:
- First run still slow
- If caching has edge-case bugs, may see stale modules (but binary_version should prevent this)

**Add a clean target for when needed**:
```makefile
sdf-clean:
	rm -rf target/
	./bin/run_sdf.sh
```

### Option B: Pre-compile Modules (Best of Both Worlds)

Create a target that pre-compiles modules without linking, allowing JIT to reuse them:

```makefile
# Pre-compile all modules to target/<binary_version>/
sdf-precompile:
	./bin/run_sdf.sh --precompile

# Run JIT mode (will use cached .o files)
sdf:
	./bin/run_sdf.sh
```

**Implementation in `run_sdf.sh`**:
```bash
if [ "$PRECOMPILE" = true ]; then
    # Compile module without running
    jank "${JANK_ARGS[@]}" compile-module vybe.sdf
    echo "Modules pre-compiled to target/"
    exit 0
fi
```

**Note**: Need to verify if `compile-module` produces `.o` files in the right location for JIT to find them.

### Option C: Hybrid AOT/JIT Mode (Most Powerful)

This is the most advanced option that would require jank compiler changes.

**Concept**: Run the AOT-compiled binary but keep the JIT available for hot-reloading.

**How it could work**:
1. Build standalone binary once
2. When running, check if any source files changed since last build
3. If no changes: run AOT binary directly (fast!)
4. If changes: JIT-compile only changed modules, overlay on AOT base

**Implementation sketch**:
```bash
# Check if standalone binary exists and is up-to-date
if [ -f "SDFViewer.app" ] && [ ! source_newer_than_binary ]; then
    # Run AOT binary directly
    open SDFViewer.app
else
    # Run JIT mode
    jank ... run-main vybe.sdf -main
fi
```

### Option D: Smarter Cache Invalidation

Instead of removing `clean-cache` entirely, make it smarter:

```makefile
sdf:
	@# Only clean if necessary
	@if [ ! -d "target" ] || [ "$(FORCE_CLEAN)" = "1" ]; then \
		rm -rf target/; \
	fi
	./bin/run_sdf.sh
```

---

## Recommended Implementation Steps

### Phase 1: Quick Win (Do First)

1. **Modify Makefile** - Remove `clean-cache` from `sdf` target
2. **Add `sdf-clean`** - For explicit cache clearing when needed
3. **Test** - Run `make sdf` twice, second run should be faster

### Phase 2: Pre-compilation (Optional Enhancement)

1. **Add `--precompile` flag** to `run_sdf.sh`
2. **Create `make sdf-precompile`** target
3. **Test** - Pre-compile, then run JIT mode

### Phase 3: Hybrid Mode (Future)

1. Requires jank compiler changes for better AOT/JIT integration
2. Would allow instant startup with hot-reload capability

---

## Commands Executed During Research

```bash
# Searched for caching code
grep -rn "target/" /Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/

# Found module loader caching logic
grep -rn "binary_cache" /Users/pfeodrippe/dev/jank/compiler+runtime/src

# Understood timestamp checking in loader.cpp
# Lines 754-796 show .o file timestamp comparison

# Checked compile_files_var for AOT compilation
grep -rn "compile_files" /Users/pfeodrippe/dev/jank/compiler+runtime/src
```

---

## Files Involved

- `Makefile` - Build targets
- `bin/run_sdf.sh` - Main run script
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/module/loader.cpp` - Module caching
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/util/environment.cpp` - Binary version hash
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/runtime/context.cpp` - Module compilation

---

## Next Steps

1. **Test Option A**: Remove `clean-cache` from `sdf` target and measure startup time
2. **If issues occur**: Investigate jank's caching behavior more deeply
3. **Consider Option B**: Add pre-compilation target for even faster startup
4. **Long-term**: Work with jank maintainer on hybrid AOT/JIT mode
