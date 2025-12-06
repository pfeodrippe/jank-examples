# jank C++ Arrow Operator (`->`) Emulation

**Date**: 2025-12-06

## Question
How to emulate C++ `->` (arrow/pointer member access) operator in jank?

## Answer

jank has **no direct `->` operator**. Combine dereference (`cpp/*`) with member access (`cpp/.-` or `cpp/.`):

### Field access: `ptr->field`
```clojure
; C++: ptr->a
; jank:
(cpp/.-a (cpp/* ptr))
```

### Method call: `ptr->method(args)`
```clojure
; C++: ptr->foo(5)
; jank:
(cpp/.foo (cpp/* ptr) 5)
```

## Quick Reference

| C++ | jank |
|-----|------|
| `obj.field` | `(cpp/.-field obj)` |
| `obj.method()` | `(cpp/.method obj)` |
| `ptr->field` | `(cpp/.-field (cpp/* ptr))` |
| `ptr->method()` | `(cpp/.method (cpp/* ptr))` |
| `&obj` | `(cpp/& obj)` |
| `*ptr` | `(cpp/* ptr)` |

## Evidence from jank tests

Found in `/Users/pfeodrippe/dev/jank/compiler+runtime/test/jank/cpp/`:
- `new/complex/pass-aggregate.jank`: `(cpp/.-a (cpp/* f))`
- `new/complex/pass-default.jank`: `(cpp/.-a (cpp/* f))`
- `operator/amp/pass-unary-anything.jank`: `(cpp/.-a (cpp/* &foo))`

## Commands Used
```bash
grep "cpp/\*.*cpp/\.-" .../test/jank/cpp  # Find ptr->field patterns
```

## Note on Clojure Threading Macros

jank also has the Clojure `->` threading macro (different concept!) in `clojure/core.jank:1836`.
