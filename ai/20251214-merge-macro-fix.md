# merge* Macro Fix - jank Regex Limitation

## Summary

Fixed the `merge*` macro in `src/vybe/util.jank` which was failing due to jank's regex handling being different from Clojure.

## The Problem

The `merge*` macro uses `str/split` to split field names by dots (e.g., `:subresourceRange.aspectMask` -> `["subresourceRange" "aspectMask"]`).

Original code:
```clojure
(str/split (name k) #"\.")
```

This produced an error in jank:
```
error: There is no '' member within 'VkImageMemoryBarrier'.
```

The empty string `''` indicated that the regex split was returning empty strings somewhere.

## The Solution

Replaced `str/split` with a custom `split-by-dot` function using `str/index-of` and `subs`:

```clojure
(defn- split-by-dot
  "Split a string by dots, returns vector of parts."
  [s]
  (loop [remaining s
         result []]
    (let [idx (str/index-of remaining ".")]
      (if idx
        (recur (subs remaining (inc idx))
               (conj result (subs remaining 0 idx)))
        (conj result remaining)))))
```

## Key Learning

**jank's regex handling in `str/split` is different from Clojure's**. When you need to split strings in jank macros, use explicit string functions (`str/index-of`, `subs`) rather than regex patterns.

## Testing

Used babashka to verify the macro expansion was correct in Clojure:
```bash
bb -e '(macroexpand-1 (quote (merge* barrier {:sType 123 :subresourceRange.aspectMask 456})))'
# => (do (cpp/= (->* barrier .-sType) 123) (cpp/= (->* barrier .-subresourceRange .-aspectMask) 456))
```

The expansion was correct, confirming the issue was jank-specific regex handling.

## Commands Used

```bash
# Test macro in babashka
bb -e '...'

# Build and test
make sdf
```

## Files Changed

- `src/vybe/util.jank` - Added `split-by-dot` helper, updated `merge*` macro

## Result

Screenshot pipeline works correctly:
```
Screenshot saved to kim_screenshot.png (640x360)
```
