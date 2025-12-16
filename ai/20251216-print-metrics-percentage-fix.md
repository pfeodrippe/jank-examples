# Print Metrics Percentage Fix

**Date**: 2025-12-16
**Status**: Completed

## Summary

Fixed `print-metrics` to show percentages relative to the main wrapper (100%), with an option for full overhead report.

## Problem

The previous implementation used `(first ...)` to find the main wrapper key, which didn't guarantee finding the key with the largest total time. This caused percentages to be incorrect (e.g., 4080% for `:BB` instead of 100%).

## Solution

Changed `print-metrics` to:
1. Find the main wrapper by sorting non-overhead metrics by total time and taking the largest
2. By default, filter out `:timed/*` overhead metrics and show percentages relative to main wrapper
3. Pass `true` to get full report with all overhead metrics

### Key Changes

```clojure
;; Before (incorrect - used arbitrary first key):
main-key (first (filter #(not (= "timed" (namespace %))) (keys metrics)))

;; After (correct - finds largest total time):
non-overhead (into {} (filter (fn [[k _]] (not (= "timed" (namespace k)))) metrics))
main-entry (first (sort-by-desc (fn [[_ v]] (:total-us v)) non-overhead))
main-total (if main-entry (:total-us (second main-entry)) 0)
```

## Usage

```clojure
;; Default - shows percentages relative to :BB (100%)
(vybe.util/print-metrics)

;; Full report - includes :timed/* overhead, percentages relative to grand total
(vybe.util/print-metrics true)
```

## Results

**Default report**:
```
=== METRICS ===
:BB                     100%  (main wrapper)
:BB/imgui-Text           11%
:BB/imgui-Checkbox        5%
...
```

**Full report**:
```
=== METRICS (FULL WITH OVERHEAD) ===
:BB                       49%
:timed/record             16%
:timed/meta-record         8%
:BB/imgui-Text             5%
...
```

## Commands Used

```bash
# Reload namespace
clj-nrepl-eval -p 5557 "(require 'vybe.util :reload)"

# Test default report
clj-nrepl-eval -p 5557 "(vybe.util/print-metrics)"

# Test full report with overhead
clj-nrepl-eval -p 5557 "(vybe.util/print-metrics true)"
```

## Files Modified

- `/Users/pfeodrippe/dev/something/src/vybe/util.jank` - Fixed `print-metrics` function (lines 222-266)

## Related

- `ai/20251216-profiling-test-expression-wrapping.md` - Previous profiling enhancements
