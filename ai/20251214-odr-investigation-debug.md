# ODR Investigation Debug Session

## Date: 2025-12-14

## Status: ✅ macOS FIXED, Linux needs libc++ fix

## Problem: CI-built .app has ODR violation (ImGui crash)

CI-downloaded app crashes with:
```
Assertion failed: (io.BackendPlatformUserData != __null && "Context or backend not initialized!
Did you call ImGui_ImplSDL3_Init()? Did you call ImGui_ImplSDL3_Shutdown()?"
```

## Key Finding: Symbol Count Difference

| Build | ImGui symbols in main binary |
|-------|------------------------------|
| CI    | 2368 (entire library!)       |
| Local | 2 (just wrapper functions)   |

The CI binary has the entire ImGui implementation embedded, while local has proper undefined references to dylib.

## Investigation Steps

1. Verified ODR fix is in place in `run_sdf.sh` (lines 756-759)
2. Verified both CI and local use same jank commit: `97fed5973007ad4ef16e43e3635208ce3d4569ad`
3. Verified same `something` code commit
4. Local build works correctly - symbols properly resolved from dylib

## Hypothesis

Something in CI environment causes jank's AOT linker to embed dylib contents instead of creating dynamic references. Possibilities:
- Stale jank build in cache
- Environment-specific LLVM/linker behavior
- Different linker flags being applied

## Changes Made

### 1. Linux CI Fix
Changed `-Wno-error=redeclared-class-member` to `-fno-access-control`:
- GCC 14's `<any>` header error is a hard error, not a warning
- `-fno-access-control` disables access specifier checking entirely

### 2. Cache Invalidation
Added `-v2` suffix to jank-build cache keys to force fresh builds.

### 3. Debug Output
Added to `run_sdf.sh`:
- Echo of full jank compile command
- ODR check that counts ImGui symbols in main binary
- Warning if ODR violation detected (>10 symbols)

## Symbols to Check

```bash
# Check for duplicate _GImGui
nm SDFViewer.app/Contents/MacOS/SDFViewer-bin 2>/dev/null | grep "_GImGui"
nm SDFViewer.app/Contents/Frameworks/libsdf_deps.dylib 2>/dev/null | grep "_GImGui"

# Count defined ImGui symbols
nm binary | grep -E " [TtDdSs] " | grep -i imgui | wc -l
```

Expected: `_GImGui` only in dylib, not main binary.

## Resolution

### macOS: FIXED ✅
- Root cause: Stale jank build in CI cache was linking symbols statically
- Fix: Invalidated cache with `-v2` suffix, fresh jank build links correctly
- Verified: CI run 20210217338 shows 0 ImGui symbols in main binary, `_GImGui` only in dylib

### Linux: Needs libc++ fix
- `-fno-access-control` doesn't work (error is about access specifier consistency, not member access)
- Solution: Use `-stdlib=libc++` and `-lc++abi` to use LLVM's libc++ instead of GCC 14's libstdc++
- Change ready locally in `.github/workflows/ci.yml` (v3 cache key)

## CI Run Results (20210217338)

| Job | Status |
|-----|--------|
| Build jank - macOS | ✅ success |
| Build jank - Linux | ❌ failure (needs libc++) |
| Build and Test - macOS | ✅ success |
| Build and Test - Linux | ⏭️ skipped |

## Next Steps

1. Push libc++ fix for Linux CI
2. Monitor CI for both platforms passing

## Commands Used

```bash
# Check CI runs
gh run view <run_id> --json jobs --jq '.jobs[] | "\(.name): \(.status) \(.conclusion)"'

# Download CI artifact
gh run download <run_id> --name "SDFViewer-macOS" -D /tmp/ci-app

# Check symbols
nm binary 2>/dev/null | grep "_GImGui"
nm binary 2>/dev/null | grep -E " [TtDdSs] " | grep -i imgui | wc -l
```
