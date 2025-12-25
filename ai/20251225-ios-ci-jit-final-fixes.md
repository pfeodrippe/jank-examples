# iOS CI & JIT Final Fixes - December 25, 2025

## Summary

Completed all fixes to make iOS simulator JIT builds work properly, including automatic staleness detection for the ios-bundle script.

## Issues Fixed

### 1. nREPL Module Not Found in JIT Mode
**Error**: `Unable to find module 'jank.nrepl-server.asio'`

**Root Cause**:
- The JIT simulator build wasn't copying `jank_aot_init.cpp` from the AOT build
- The staleness check didn't detect when `ios-bundle` script changed

**Fix in Makefile**:
```makefile
# Added copy step to ios-jit-sim-libs
ios-jit-sim-libs: ios-jit-sim-aot
	# ... existing lib copies ...
	@if [ -f "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
		cp SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp SdfViewerMobile/build-iphonesimulator-jit/generated/; \
	fi

# Added ios-bundle timestamp check to staleness detection
ios-jit-sim-aot:
	@if [ ... ] || \
	   [ "$(JANK_SRC)/bin/ios-bundle" -nt "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
		./SdfViewerMobile/build_ios_jank_aot.sh simulator; \
	fi
```

### 2. Shell Scripts Gitignored
**Error**: `build-ios-pch.sh: No such file or directory` in CI

**Root Cause**: Pattern `SDFViewerMobile/build-*` was ignoring shell scripts

**Fix in .gitignore**:
```
SDFViewerMobile/build-*
!SDFViewerMobile/*.sh
```

### 3. rsync Missing Directories
**Error**: rsync failed in CI because target directories didn't exist

**Fix**: Added `mkdir -p` before all rsync commands in `ios-jit-sync-sources` and `ios-jit-sync-includes` targets.

### 4. ios-bundle Swallowing Errors
**Error**: Module discovery failed silently (grep filtered error output)

**Fix in jank compiler** (`/Users/pfeodrippe/dev/jank/compiler+runtime/bin/ios-bundle`):
```bash
# Capture exit code before piping through grep
if ! module_output=$("${native_jank}" ... 2>&1); then
    echo "[ios-bundle] ERROR: Module discovery failed!"
    echo "${module_output}"
    exit 1
fi
```

## Commands Used

```bash
# Run iOS simulator JIT build
make ios-jit-sim-run

# View CI logs
gh run view <run_id> --repo pfeodrippe/jank-examples --log-failed
```

## Verification

Successfully ran `make ios-jit-sim-run`:
```
** BUILD SUCCEEDED **
Launching simulator...
```

## What's Next

1. **Commit jank repo changes**: The ios-bundle error handling fix needs to be committed and pushed to the nrepl branch
2. **Monitor CI**: Push changes and verify all iOS CI jobs pass (both AOT and JIT builds)

## Key Learnings

1. Makefile staleness detection should include all relevant inputs (source files, build scripts, etc.)
2. When copying generated files between build configurations, ensure the copy step is in the dependency chain
3. rsync requires target directories to exist beforehand
4. Negative gitignore patterns (`!pattern`) can un-ignore files matched by earlier patterns
