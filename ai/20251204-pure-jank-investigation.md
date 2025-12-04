# Pure Jank vs cpp/raw Investigation

**Date**: 2025-12-04

## Summary

Investigated whether jolt wrapper functions can be written in pure jank instead of cpp/raw. The investigation was **partially successful** - some patterns work in pure jank, but the approach was abandoned before full completion.

## Key Discovery

**Pure jank CAN handle opaque_box extraction and float conversion!**

The following patterns WORK in pure jank:

```clojure
;; Extract void* from opaque_box
(cpp/unbox (cpp/type "void*") world-box)

;; Convert double to float
(cpp/cast cpp/float dt)

;; Handle void returns with _ binding
(let* [_ (some-void-function ...)]
  nil)
```

## Tested and Working

The `step!` function was successfully converted to pure jank and **physics was verified working** through screenshot comparison:

```clojure
(defn step!
  [w dt]
  (let* [ptr (cpp/unbox (cpp/type "void*") w)
         dt-float (cpp/cast cpp/float dt)
         _ (jolt/jolt_world_step ptr dt-float 1)]
    nil))
```

**Proof**: Screenshots at frames 60 and 180 had different MD5 hashes, confirming physics was stepping.

## Syntax Requirements Discovered

1. **cpp/unbox needs explicit type**: `(cpp/unbox (cpp/type "void*") w)` - NOT `(cpp/unbox w)`
2. **void* needs cpp/type**: `(cpp/type "void*")` - NOT `cpp/void*`
3. **Void returns need _ binding**: Must use `(let* [_ (void-fn ...)] nil)` pattern

## What Was NOT Tested

Due to user request to stop, these were not fully tested:
- create-sphere, create-floor (float params)
- set-velocity! (float params)
- num-bodies, num-active (return int)

## Current State

Reverted to cpp/raw wrappers for stability. The existing cpp/raw wrappers are justified because:
1. They handle opaque_box extraction reliably
2. They handle double->float conversion
3. They're already working and tested

## Conclusion

Pure jank approach IS possible for jolt wrappers, but:
1. Requires more verbose syntax (`cpp/type "void*"`, `cpp/cast cpp/float`, `let*` with `_`)
2. cpp/raw wrappers are simpler and already working
3. Trade-off: More jank code vs cpp/raw block

The native resources guide (20251202-native-resources-guide.md) overstates the need for cpp/raw - pure jank alternatives exist for opaque_box and float conversion.

## Commands Used

```bash
./run_integrated.sh 2>&1 | head -80
md5 screenshot_frame60.png screenshot_frame180.png
```

## Files Modified

- `/Users/pfeodrippe/dev/something/src/my_integrated_demo.jank` (reverted)
