# CI PCH Cache Fix - 2025-12-17

## Problem

The Linux CI build was failing with:
```
error: use of undeclared identifier 'jank_unbox_lazy_source'; did you mean 'jank_unbox_with_source'?
fatal error: file '/home/runner/jank/compiler+runtime/include/cpp/jank/c_api.h' has been modified since the precompiled header '/home/runner/jank/compiler+runtime/build/incremental.pch' was built
```

## Root Cause

The PCH (precompiled header) cache key was using the branch name instead of the commit hash:
```yaml
key: jank-pch-linux-x64-${{ env.JANK_REF }}-v1  # JANK_REF = "nrepl"
```

When new commits were pushed to the jank repo (adding `jank_unbox_lazy_source` to `c_api.h`), the PCH cache was not invalidated because the branch name didn't change.

## Fix

Since PCH takes 1h30 to build, we don't want to invalidate it on every commit. Instead:

1. **Use commit-based key with fallback**: Changed cache key to use commit hash, but added `restore-keys` to fall back to any previous PCH:
   ```yaml
   key: jank-pch-linux-x64-${{ needs.get-jank-commit.outputs.commit }}-v1
   restore-keys: |
     jank-pch-linux-x64-
   ```

This way:
- If exact commit cache exists → use it (fast)
- If only older PCH exists → restore it as fallback, ninja will rebuild it since `jank-phase-1` dependency is newer
- PCH only fully rebuilds when jank rebuilds (when there's a new commit)

## Bonus: Incremental Builds

The jank build cache was missing object files (`.o`), so every CI run did a full rebuild from scratch (1h30+). Added:
```yaml
~/jank/compiler+runtime/build/*.o
~/jank/compiler+runtime/build/**/*.o
```

Also added `restore-keys` to jank build cache so it falls back to older builds, enabling ninja to do incremental compilation (only rebuild changed files).

## What I Learned

- The jank build cache was correctly keyed by commit hash but missing `.o` files for incremental builds
- PCH files are very sensitive to header changes and must be rebuilt when c_api.h changes
- Using `restore-keys` allows fallback to older caches while still tracking exact versions
- In `CMakeLists.txt:1212`, the PCH depends on `jank-phase-1`. When jank is rebuilt, ninja detects the dependency is newer and rebuilds the PCH automatically
- Without object files cached, ninja rebuilds everything from scratch each time
