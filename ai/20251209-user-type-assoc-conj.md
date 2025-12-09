# User-Type assoc/conj Support for defcomp

## Date: 2025-12-09

## Summary

Added `:assoc` and `:conj` support to user-type based component instances, enabling `merge` and `assoc` to work directly on standalone component instances.

## Changes

Updated `create-comp-user-type` in `src/vybe/type.jank` to include:

### :assoc behavior
```clojure
:assoc (fn [this k v]
         (when (contains? field-names k)
           (let [offset (field-offset comp (name k))
                 field-type (get field-types k)]
             (when offset
               (write-field-to-ptr! ptr-box field-type offset v))))
         this)
```
- Writes value directly to native memory at field offset
- Returns `this` (mutable update, same instance)
- Ignores unknown fields silently

### :conj behavior
```clojure
:conj (fn [this entry]
        (if (map? entry)
          ;; merge calls (conj m1 m2) with a map
          (doseq [[k v] entry]
            (when (contains? field-names k)
              (let [offset (field-offset comp (name k))
                    field-type (get field-types k)]
                (when offset
                  (write-field-to-ptr! ptr-box field-type offset v)))))
          ;; single [k v] entry
          (let [k (first entry)
                v (second entry)]
            (when (contains? field-names k)
              (let [offset (field-offset comp (name k))
                    field-type (get field-types k)]
                (when offset
                  (write-field-to-ptr! ptr-box field-type offset v))))))
        this)
```
- Handles both `[k v]` pairs AND maps (for `merge` support)
- `merge` calls `(conj m1 m2)` with a map, so we iterate over all entries
- Writes to native memory
- Returns `this`

## Usage

```clojure
(defcomp Position [[:x :float] [:y :float]])

(def pos (Position {:x 1.0 :y 2.0}))

;; Now these work:
(assoc pos :x 99.0)      ;; mutates and returns pos
(merge pos {:x 50.0})    ;; merge calls assoc behind the scenes

;; Reads reflect updated values:
(:x pos)  ;; => 99.0 (or 50.0 after merge)
```

### :to-string behavior (updated)
```clojure
:to-string (fn [this]
             (let [field-strs (map (fn [f]
                                     (let [k (keyword (:name f))
                                           offset (field-offset comp (:name f))
                                           field-type (get field-types k)
                                           v (read-field-from-ptr ptr-box field-type offset)]
                                       (str k " " v)))
                                   (:fields comp))]
               (str "#" comp-name "{" (apply str (interpose ", " field-strs)) "}")))
```
- Now shows actual field values read from native memory
- Example: `#vybe_type_test__Position{:x 42.5, :y 99.25}`

## Notes

- Both `:assoc` and `:conj` perform **mutable updates** to native memory
- They return `this` rather than creating a new instance
- This matches the semantics needed for `merge` to work with mutable backing store
- The user-type factory is created fresh each call to avoid stale closure issues on REPL re-evaluation

## Tests

All 19 type tests pass (81 assertions).

## Commands

```bash
./run_tests.sh
```
