# Fiction Navigation and Speaker Name Fixes

**Date:** 2026-02-04

## Summary

Fixed navigation logic for the Disco Elysium-style narrative game and fixed speaker name display. Navigation now works correctly: choices without nested choices stay at the same level, choices with nested choices navigate deeper. Users can re-select any choice to see its content again.

## Final Navigation Behavior

1. **Select a choice** → Always shows its content and adds to history
2. **If choice has nested choices** → Navigate into that level (show nested choices)
3. **If choice has NO nested choices** → Stay at current level (same choices remain visible)
4. **Re-selecting a choice** → Shows the same content again (no restrictions)

## Changes Made

### src/fiction/state.jank

1. **Updated `load-story!`** to initialize:
   - `:node-stack []` - Stack of parent nodes for navigation back
   - `:selected-ids #{}` - Set of already-selected choice IDs (for grey styling)

2. **Added `get-selected-ids` accessor** to expose selected IDs to render.jank

3. **Simplified `select-choice!`**:
   - Always adds choice to history and reveals content (allows re-selection)
   - Checks if choice has nested choices via `(some #(= (:type %) :choice) (:children choice))`
   - Only navigates deeper (push stack, change current-node) if nested choices exist
   - No automatic pop-back logic - user controls navigation

4. **Removed `pop-to-parent-with-choices!`** - No longer needed

### src/fiction/render.jank

1. **Modified `add-choice-entry!`** to pass `selected-ids` and mark already-selected choices

2. **Modified `build-dialogue-vertices!`** to get `selected-ids` from state

### src/fiction/parser.jank

1. **Fixed `parse-character-prefix`** - UTF-8 handling bug:
   - Was using `(subs trimmed 2)` which treated string as byte-indexed
   - `#∆` is 4 bytes (# = 1 byte, ∆ = 3 bytes UTF-8)
   - Fixed by using `(count prefix)` to get character length

2. **Fixed `parse-frontmatter`** - Indentation detection bug:
   - Was checking `(str/starts-with? k "  ")` on already-trimmed key
   - Fixed by checking indentation on raw line before trimming

### vulkan/fiction_text.hpp

1. **Added `add_choice_entry_with_selected(const char* text, bool selected)`**:
   - `EntryType::ChoiceSelected` when selected=true (muted grey)
   - `EntryType::Choice` when selected=false (orange)

2. **Removed `" ---"` separator** from speaker name display

## Bugs Fixed

### Speaker Name Display ("??V ---" instead of "Voiture")

**Root cause:** Two issues:
1. `parse-frontmatter` wasn't detecting indented lines correctly
2. `parse-character-prefix` was using byte offset instead of character count for UTF-8

### Navigation Going to Wrong Level

**Root cause:** Was navigating into every choice and then trying to pop back.

**Fix:** Only navigate deeper if choice has nested choices; otherwise stay at same level.

## Key jank Limitation Discovered

- `subs` function uses byte offsets, not character offsets for UTF-8 strings
- Use `(count string)` to get character count, which works correctly for UTF-8

## Files Modified

- `src/fiction/state.jank` - Navigation logic
- `src/fiction/render.jank` - Choice rendering with selected status  
- `src/fiction/parser.jank` - Fixed UTF-8 and indentation parsing
- `vulkan/fiction_text.hpp` - C++ function for selected choices, removed "---" separator

## Next Steps

- Add explicit "go back" text/action to return to parent level
- User will implement this later
