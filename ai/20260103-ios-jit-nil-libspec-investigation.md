# iOS JIT "not a libspec: nil" Investigation

**Date**: 2026-01-03
**Issue**: iOS JIT failing with `{:error :assertion-failure, :data {:msg "not a libspec: nil"}}` when loading vybe.util

## Investigation Summary

### Files Examined
- `/Users/pfeodrippe/dev/jank/compiler+runtime/src/jank/clojure/core.jank` - Source has nil filter
- `/Users/pfeodrippe/dev/something/SdfViewerMobile/build-iphonesimulator-jit/clojure_core_generated.cpp` - Generated code also has nil filter

### Key Findings

1. **Nil filter is present in source** (lines 4563-4565):
```clojure
args (remove #(or (keyword? %)
                  (nil? %)
                  (and (vector? %) (not (libspec? %)) (not (native-header-libspec? %)))) args)
```

2. **Nil filter is present in generated code**:
```cpp
jank::runtime::make_box(jank::runtime::is_nil(_PERC_1_SHARP_))
```
This is in the filter predicate `fn_113281` used by `remove` in load-libs.

3. **Timestamps are correct**:
- Nil filter commit b3e45f967: 2026-01-03 11:04:55
- Jank binary (jank-phase-1): 2026-01-03 11:26
- iOS JIT clojure_core_generated.cpp: 2026-01-03 11:29

4. **vybe.util require form looks valid**:
```clojure
(ns vybe.util
  (:require
   [clojure.string :as str]))
```

### Mystery

The nil filter should be working, but somehow nil is reaching the throw-if. Possible causes:
1. Something in the JIT compilation path generates nil that bypasses the filter
2. There's a cached version somewhere that doesn't have the fix
3. The lazy sequence evaluation has a race condition
4. The ns macro expansion on the compile server produces nil in the args

## Commands to Clean Everything

```bash
# Clean iOS JIT artifacts
make ios-jit-clean

# Clean jank's main incremental.pch (might help)
rm -f /Users/pfeodrippe/dev/jank/compiler+runtime/build/incremental.pch

# Rebuild jank binary (to ensure clojure.core is freshly loaded)
cd /Users/pfeodrippe/dev/jank/compiler+runtime && ./bin/compile

# Then rebuild iOS JIT
make ios-jit-sim-run
```

## Next Steps

1. Try complete clean rebuild including jank binary
2. Add debug println in load-libs to see what args it receives
3. Check if the ns macro expansion is correct on compile server side
