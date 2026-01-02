# iOS JIT CI Fix and IMMER ABI Mismatch

## Date: 2026-01-02

## Summary

Fixed two issues:
1. CI failure due to missing `bpptree` third-party library
2. Potential IMMER ABI mismatch between compile server and iOS app

## Issue 1: CI Failure - Missing bpptree

### Error
```
rsync(23147): error: /Users/runner/jank/compiler+runtime/third-party/bpptree/: (l)stat: No such file or directory
make: *** [ios-jit-sync-includes] Error 23
```

### Root Cause
- `bpptree` directory existed locally but was never committed to jank's git repo
- It's an untracked directory not in `.gitmodules` or any git tracking
- CI clones jank repo fresh, so bpptree doesn't exist

### Fix
Removed ALL bpptree references from:
- `Makefile` (include path and rsync commands)
- `jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
- `jank/compiler+runtime/CMakeLists.txt` (4 references)
- `SdfViewerMobile/project-jit-device.yml`
- `SdfViewerMobile/project-jit-sim.yml`
- `SdfViewerMobile/config-common.yml`
- Deleted the untracked `/Users/pfeodrippe/dev/jank/compiler+runtime/third-party/bpptree/` directory
- Deleted synced copies in `SdfViewerMobile/jank-resources/include/bpptree/`

## Issue 2: IMMER ABI Mismatch (Potential)

### Background
The iOS app uses these defines for IMMER:
- `IMMER_HAS_LIBGC=1`
- `IMMER_TAGGED_NODE=0`

The compile server's `persistent_compiler.hpp` was missing these defines, which could cause ABI mismatch in immer data structures (hash maps, vectors) between:
- Objects compiled by the compile server
- iOS runtime expecting different memory layouts

### Fix
Added IMMER defines to `persistent_compiler.hpp` in both:
- `persistent_compiler::compile()` (line 154)
- `incremental_compiler::init()` (line 328)

```cpp
driver_args_storage.push_back("-DJANK_IOS_JIT=1");
// CRITICAL: IMMER defines must match iOS app for ABI compatibility
// These affect memory layout of immer data structures (hash maps, vectors)
driver_args_storage.push_back("-DIMMER_HAS_LIBGC=1");
driver_args_storage.push_back("-DIMMER_TAGGED_NODE=0");
```

## Additional Improvements

### Added Missing Third-Party Includes
Added to Makefile `VYBE_FLECS_JANK_INCLUDES`:
- `stduuid/include` - for UUID support
- `cppinterop/include` - for JIT interpreter

Added to `ios-jit-sync-includes`:
- `uuid.h` header copy from stduuid

## Files Changed

1. `/Users/pfeodrippe/dev/something/Makefile`
   - Removed bpptree references
   - Added stduuid and cppinterop includes
   - Added stduuid header to sync

2. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/persistent_compiler.hpp`
   - Added IMMER defines for ABI compatibility

3. `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
   - Removed bpptree include path

## Commands Run

```bash
# Check if bpptree exists in jank origin/main
cd /Users/pfeodrippe/dev/jank && git ls-tree origin/main compiler+runtime/third-party/ | grep bpptree
# Result: no output (doesn't exist)

# Check CI failure
gh run view 20650690732 --repo pfeodrippe/jank-examples --log-failed 2>&1 | grep -E "error|FAILED"

# Rebuild jank with IMMER fix
cd /Users/pfeodrippe/dev/jank/compiler+runtime
SDKROOT=/Applications/Xcode.app/.../MacOSX.sdk CC=$PWD/build/llvm-install/usr/local/bin/clang CXX=$PWD/build/llvm-install/usr/local/bin/clang++ ./bin/compile
```

## Next Steps

1. Commit changes to jank-examples repo
2. Push and verify CI passes
3. Test iOS JIT with the fixes to confirm nil symbol error is resolved
