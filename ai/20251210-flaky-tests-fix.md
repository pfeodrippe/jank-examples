# 2025-12-10 Flaky vybe tests fix

## What was flaky
- `run_tests.sh` runs `vybe.flecs-test` then `vybe.type-test`.
- `world-map-get-in-test` intermittently failed because components were missing after world recreation; `Position` data was nil.

## Root cause
- Component ID cache (`comp-id-cache`) keyed by world pointer reused stale entries when a fresh `ecs_world_t*` reused the same address.
- Cached IDs pointed at unregistered components in the new world, so lookups returned nil and subsequent `get-in` failed.

## Fix implemented
- Added C++ helper `vybe_has_type_info` to verify a component is registered in a world.
- `comp-id` now validates cached IDs via `vybe_has_type_info`; if invalid, it removes the stale cache entry and re-registers the component. This keeps the cache in sync across reused world pointers.
- Kept all changes in `src/vybe/type.jank` (plus the embedded C++ block).

## Validation
- Ran `for i in {1..5}; do ./run_tests.sh; done` and all 5 iterations passed with zero failures.

## Notes and lessons
- When caching native IDs keyed by world pointer, always validate cache entries against the current world to handle pointer reuse.
- Avoid non-ASCII punctuation in source; lexer rejected a Unicode dash in a comment during this work.

## Follow-ups (optional)
- Consider clearing component caches when worlds are destroyed, if lifetime hooks become available.
- Add a small regression test that creates two worlds sequentially and ensures `comp-id` re-registers components correctly when addresses are reused.
