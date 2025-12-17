# Print Metrics Enhancements

**Date**: 2025-12-16
**Status**: Completed

## Summary

Enhanced `print-metrics` with:
1. Percentages relative to real work (excluding overhead)
2. Hierarchical display with `*` for nesting levels
3. Keys at end of each row
4. Children sorted by total time, displayed below parent

## Changes

### 1. Real Work Percentages
- Child operations now sum to ~100% of measured work
- Main wrapper shown separately with overhead breakdown

### 2. Hierarchical Display
- Parent keys (like `:AA`) shown first
- Children marked with `*` prefix (e.g., `* imgui-Text`)
- Deeper nesting would use `**`, `***`, etc.

### 3. New Output Format
```
=== METRICS (REAL WORK) ===
Main wrapper: 74559.921 ms
Real work: 39120.838 ms (52%)
Overhead: 37743.597 ms (50%)
     Count     Total(ms)     %   Avg(us)     Min     Max  Key
--------------------------------------------------------------------------------
      1276  74559.921000  190%     58432   53986   73667  AA
     15322   8552.685000   21%       558       1   11571  * imgui-Text
      8934   4274.356000   10%       478     428    9061  * imgui-Checkbox
      ...
```

## Key Functions Added

- `format-metric-row` - Formats row with key at end and `*` prefix
- `build-hierarchy` - Builds tree from namespace relationships
- `get-nesting-level` - Calculates `*` count for a key

## Usage

```clojure
;; Default - real work percentages, hierarchical
(vybe.util/print-metrics)

;; Full report with overhead metrics
(vybe.util/print-metrics true)
```

## Files Modified

- `/Users/pfeodrippe/dev/something/src/vybe/util.jank` - `print-metrics` and helper functions

## Next Steps

To get deeper nesting (e.g., `cpp-float` under `imgui-Text`), `wrap-form` would need to generate hierarchical keys like `:AA/imgui-Text/cpp-float` based on call context.
