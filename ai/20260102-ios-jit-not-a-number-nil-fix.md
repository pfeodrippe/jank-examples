# iOS JIT "not a number: nil" Error Fix - 2026-01-02

## Summary
After fixing the lifted constants bug, iOS JIT now loads all modules successfully. However, a new runtime error occurs: `not a number: nil`.

## Error Message
```
[jank] binding_scope pop failed: Mismatched thread binding pop
  current *ns*: clojure.core
  current *file*: "NO_SOURCE_PATH"
[jank] Error calling -main (std): not a number: nil
```

## Progress Made
1. **Fixed lifted constants bug** - Modules now load correctly!
   - The debug output shows correct values: `[DEBUG CONST] vybe::util::const_75926 = [clojure.string :as str]`
   - All 9 modules load and their entry functions complete

2. **Error occurs during runtime execution** - After modules load, during the main loop

## Current Analysis

### Error Location
The error `not a number: nil` comes from `runtime/core/math.cpp:1921` when a math operation receives nil.

### Suspected Call Path
1. `vybe.sdf.ios/-main` calls `run!`
2. `run!` calls `init-kim!` (succeeds, prints "Starting iOS viewer...")
3. `run!` enters the loop and calls `(draw 0)`
4. In `draw`, something receives nil where a number is expected

### Possible Causes
1. **sync-camera-from-cpp!** - May return nil values from C++ engine
2. **state/update-camera!** - If camera map has nil values, `(+ nil 0.01)` fails
3. **ui/auto-rotate?** - Uses pointer dereference which might fail
4. **Binding scope mismatch** - The binding_scope pop failure suggests deeper issue

### Key Observations
- The `binding_scope pop failed` error happens BEFORE the math error
- This suggests thread binding issues during module loading
- Might be related to how defonce/atom are initialized in JIT mode

## Investigation Steps

1. Check if defonce atoms are properly initialized in JIT mode
2. Compare AOT vs JIT generated code for state module's defonce calls
3. Look at sync-camera-from-cpp! to see if it returns nil
4. Check if the binding_scope mismatch is causing nil values

## Commands
```bash
# Build jank
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Run iOS JIT
make ios-jit-clean && make ios-jit-sim-run

# Format generated code for inspection
clang-format /tmp/jank-debug-dep-MODULE.cpp > /tmp/jank-debug-dep-MODULE-formatted.cpp
```

## Next Steps
1. Add debug output to print camera values before math operations
2. Check if the issue is with defonce not properly guarding re-initialization
3. Compare how binding scopes work in AOT vs JIT mode
