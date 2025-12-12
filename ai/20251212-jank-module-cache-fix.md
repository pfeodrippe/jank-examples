# jank Module Cache & Standalone Build - Dec 12, 2025

## Problem 1: JIT Mode Cache Issue (SOLVED)
`./bin/run_sdf.sh` was failing with:
```
Failed to find symbol: 'jank_load_vybe_flecs'
```

### Root Cause
**Stale compiled modules in the `target/` directory.**

jank caches compiled `.o` files for modules in `target/<platform-hash>/`. When jank finds a pre-compiled `.o` file for a module, it uses `load_o` instead of `load_jank` (compiling from source).

### Fix
Clear the target cache:
```bash
rm -rf target/
# or
make clean-cache
```

### Additional Fix
Changed `-l` to `--lib` in `bin/run_sdf.sh` JIT mode:
- `-l` expects library names (e.g., `-lvulkan`)
- `--lib` accepts full paths (e.g., `--lib /opt/homebrew/lib/libvulkan.dylib`)

---

## Problem 2: Standalone Build ODR Violations (KNOWN JANK BUG)
`./bin/run_sdf.sh --standalone` fails with C++ redefinition errors for functions in cpp/raw blocks.

### Root Cause
jank's standalone compilation has two phases:
1. **JIT phase** - compiles and runs code (defines functions in `input_line_*`)
2. **AOT phase** - generates C++ from .jank source (defines same functions again)

Both outputs end up in the same compilation unit, causing ODR violations even with `inline`.

### Status
This is a **jank compiler bug**. Workarounds tried:
- `inline` keyword: Still causes redefinition errors (both copies visible)
- `static inline`: Breaks JIT mode (separate callback registries)
- Include guards: Conflict with jank's internal `#ifndef JANK_CPP_RAW_*` guards

**JIT mode works fine.** Standalone mode with cpp/raw blocks that define functions is broken.

---

## Commands
```bash
# JIT mode (works)
make sdf              # Run SDF viewer
make integrated       # Run integrated demo
make tests            # Run tests (all pass)
make clean-cache      # Clear stale module cache

# Standalone mode (broken for vybe.flecs due to jank bug)
make sdf-standalone   # Will fail with ODR violations
```

## Troubleshooting
If you get "Failed to find symbol: 'jank_load_*'" errors:
```bash
make clean-cache
```
