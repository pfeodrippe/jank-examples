# Testing Workflow

## After modifying integrated example

**ALWAYS run `./bin/run_integrated.sh` after modifying `src/my_integrated_demo.jank`** to verify changes work correctly.

```bash
./bin/run_integrated.sh
```

If debugging is needed:
```bash
./bin/run_integrated.sh --lldb   # Shows jank source locations in stack traces
./bin/run_integrated.sh --debug  # Enable debug symbols
```

## Common issues
- Type errors (e.g., passing float where int expected) - use `(int ...)` casts
- Array indexing - use `(->* arr (aget idx) &)` for `&arr[idx]`
- `bit-or` returns jank object - wrap with `(cpp/int ...)` for C++ assignment
- `loop/recur` with native types - use `cpp/raw` for while loops with native ints
- Void-returning functions in `let*` - bind to `_` (e.g., `_ (imgui/CreateContext)`)