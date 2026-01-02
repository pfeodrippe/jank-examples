# CI Fix: Incorrect jank CLI Syntax in Test Script

**Date**: 2026-01-01
**CI Run**: https://github.com/pfeodrippe/jank-examples/actions/runs/20650127262/job/59293689321
**Status**: FAILED at test step
**Error**: `error: Extra positional args: -main`

## Problem

After all cache fixes, the CI successfully:
- ✅ Built jank-phase-1
- ✅ AOT compiled core libraries
- ✅ Cached everything correctly
- ✅ Restored cache in dependent jobs
- ✅ Verified jank binary
- ✅ Started running tests

But tests failed with:
```
=== Running vybe.flecs-test ===
error: Extra positional args: -main
make: *** [test] Error 1
```

## Root Cause

In `bin/run_tests.sh` lines 63 and 76, the test commands were:

```bash
jank run-main vybe.flecs-test -main  # ❌ Wrong!
jank run-main vybe.type-test -main   # ❌ Wrong!
```

The `-main` at the end is an **extra positional argument** that jank doesn't expect.

### Why This Happened

Looking at `jank --help`:
```
run-main    Load and execute -main.
```

The `run-main` command **automatically** looks for and executes the `-main` function in the specified namespace. Adding `-main` as an extra argument confuses the CLI parser.

This was probably:
1. Legacy syntax from an older version of jank, or
2. A misunderstanding of the CLI syntax

### Correct Syntax

```bash
jank run-main vybe.flecs-test  # ✅ Correct - finds and runs -main automatically
jank run-main vybe.type-test   # ✅ Correct
```

The `run-main` command implicitly knows to execute the `-main` function.

## Solution

Updated `bin/run_tests.sh`:

**Before** (lines 58-64):
```bash
JANK_ARGS=(
    -I./vendor/flecs/distr
    -I./vendor
    $OBJ_ARGS
    --module-path src:test
    run-main vybe.flecs-test -main  # ❌ Extra -main
)
```

**After**:
```bash
JANK_ARGS=(
    -I./vendor/flecs/distr
    -I./vendor
    $OBJ_ARGS
    --module-path src:test
    run-main vybe.flecs-test  # ✅ Removed -main
)
```

Same fix applied to `JANK_TYPE_ARGS` on line 76.

## Files Modified

- `~/dev/something/bin/run_tests.sh` - Removed extra `-main` arguments (2 locations)

## Complete Fix Timeline

| # | Issue | Fix | File | Status |
|---|-------|-----|------|--------|
| 1 | Phase 2 enabled before core.cpp exists | Check if core.cpp exists before enabling phase 2 | jank/CMakeLists.txt | ✅ |
| 2 | core-libs not cached | Add core-libs to cache paths | ci.yml (e076944) | ✅ |
| 3 | jank-phase-1 binary not cached | Add jank-phase-1 and classes to cache | ci.yml (v5) | ✅ |
| 4 | Incorrect test CLI syntax | Remove extra `-main` args | run_tests.sh | ✅ |

## Why This Wasn't Caught Locally

This test script probably worked with an older version of jank where the CLI syntax was different, or it was never tested with the `run-main` command. The CI caught it because it's running with the latest jank from the nrepl branch.

## Expected Outcome

With this fix, tests should:
1. Build vybe_flecs_jank.o (if needed)
2. Run `jank run-main vybe.flecs-test` successfully
3. Run `jank run-main vybe.type-test` successfully
4. Print "All tests passed!"
5. CI should go green! 🟢
