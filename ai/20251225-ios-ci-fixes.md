# iOS CI Fixes - December 25, 2025

## Problem

CI jobs for iOS builds were failing:

1. **AOT Build**: Failed silently during "Discovering modules from vybe.sdf.ios..." with no visible error
2. **JIT Build**: Failed with rsync error - target directory didn't exist

## Root Causes

### AOT Build
1. The `ios-bundle` script was swallowing errors during module discovery because grep filtered out the output
2. The `sdf-ios-sim` target depended on `build-sdf-deps` but `build_ios_jank_aot.sh` requires `libsdf_deps.dylib` which is only built by `build-sdf-deps-standalone`

### JIT Build
1. rsync doesn't create nested directory paths - `SdfViewerMobile/jank-resources/src/jank/vybe/` didn't exist

## Fixes Applied

### 1. ios-bundle error handling (jank compiler)
**File**: `/Users/pfeodrippe/dev/jank/compiler+runtime/bin/ios-bundle`

Changed module discovery to capture and show errors instead of filtering them:
```bash
# Before (errors swallowed by grep)
all_modules=$("${native_jank}" ... 2>&1 | grep -v "^\[jank" | grep -v "^WARNING:")

# After (errors shown before grep)
if ! module_output=$("${native_jank}" ... 2>&1); then
    echo "[ios-bundle] ERROR: Module discovery failed!"
    echo "${module_output}"
    exit 1
fi
all_modules=$(echo "${module_output}" | grep -v ...)
```

### 2. Makefile: iOS target dependencies
**File**: `Makefile`

Changed iOS targets to depend on `build-sdf-deps-standalone` instead of `build-sdf-deps`:
```makefile
# Before
sdf-ios-sim: build-sdf-deps
sdf-ios-device: build-sdf-deps

# After
sdf-ios-sim: build-sdf-deps-standalone
sdf-ios-device: build-sdf-deps-standalone
```

### 3. Makefile: JIT sync directories
**File**: `Makefile`

Added `mkdir -p` before rsync commands:
```makefile
ios-jit-sync-sources:
	@mkdir -p SdfViewerMobile/jank-resources/src/jank/vybe
	@rsync -av ...

ios-jit-sync-includes:
	@mkdir -p SdfViewerMobile/jank-resources/include/gc
	@mkdir -p SdfViewerMobile/jank-resources/include/immer
	# ... etc
```

## Commands Used

```bash
# View CI logs
gh run view 20508854852 --repo pfeodrippe/jank-examples --log-failed

# Search for specific patterns in logs
gh run view ... 2>&1 | grep -A 50 "Discovering modules"
```

## Key Learnings

1. When piping command output through grep, capture exit code first to avoid swallowing errors
2. rsync requires target directories to exist (or use specific flags)
3. JIT lib dependencies (`--jit-lib`) are needed during module discovery, not just compilation
4. `build-sdf-deps` vs `build-sdf-deps-standalone` - the standalone version creates the dylib needed for JIT symbol resolution

## What's Next

Push changes and monitor CI to ensure both iOS AOT and JIT builds pass.
