# Fix make integrated - 2025-12-14

## Problem

`make integrated` was failing with two issues:

1. **CMake compiler error**: CMake couldn't find the C/C++ compiler because it was picking up `CC` and `CXX` environment variables pointing to non-existent jank compiler paths:
   ```
   CC=/Users/pfeodrippe/dev/jank/build/llvm-install/usr/local/bin/clang
   CXX=/Users/pfeodrippe/dev/jank/build/llvm-install/usr/local/bin/clang++
   ```

2. **Missing raylib library**: The raylib distribution directory didn't exist (`vendor/raylib/distr/`)

## Solution

### Fix 1: Explicit compiler paths in build_jolt.sh

Added explicit system compiler exports at the top of `build_jolt.sh`:

```bash
# Use system clang (avoid inheriting jank's custom CC/CXX)
export CC=/usr/bin/clang
export CXX=/usr/bin/clang++
```

### Fix 2: Build raylib

Ran `./build_raylib.sh` to create the raylib libraries.

## Commands Used

```bash
make integrated                  # Initial failure
./build_raylib.sh               # Build raylib
make integrated                  # Success
```

## Files Modified

- `build_jolt.sh` - Added CC/CXX exports

## Next Steps

The integrated demo now runs successfully. If similar issues occur with other build scripts, apply the same fix of explicitly setting CC/CXX to system clang.
