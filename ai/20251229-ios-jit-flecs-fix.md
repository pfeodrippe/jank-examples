# iOS JIT Project Files Missing Flecs Sources

## Problem
GitHub Actions CI job "Build iOS JIT Simulator" failing with undefined symbol errors:
- `_ecs_init`, `_ecs_fini`, `_ecs_add_id`, etc.
- `vybe_entity_ids`, `vybe_children_ids`, `vybe_all_named_entities`, etc.
- `_FLECS_IDecs_entity_tID_`, `_FLECS_IDecs_f32_tID_`, etc.

## Root Cause
The JIT project YAML files were missing Flecs source files that were already present in:
- `project.yml` (AOT)
- `project-jit-only-sim.yml`
- `project-jit-only-device.yml`

Missing in:
- `project-jit-sim.yml` ← **CI uses this one**
- `project-jit-device.yml`
- `project-jit.yml`

## Fix
Added Flecs sources to all three files after `stb_impl.c`:

```yaml
# Flecs ECS (needed for vybe.type and vybe.flecs)
- path: ../vendor/flecs/distr/flecs.c
  compilerFlags: ["-O2"]
# Vybe Flecs helpers (needed for vybe.flecs jank bindings)
- path: ../vendor/vybe/vybe_flecs_jank.cpp
  compilerFlags: ["-O2"]
```

## Files Modified
- `SdfViewerMobile/project-jit-sim.yml`
- `SdfViewerMobile/project-jit-device.yml`
- `SdfViewerMobile/project-jit.yml`

## How to Verify
```bash
for f in SdfViewerMobile/project-jit*.yml; do
  echo "=== $(basename $f) ==="
  grep -c "flecs.c" "$f" || echo "MISSING"
done
```
All should show `1`.

## Commands
- View CI job logs: `gh run view <run_id> --repo pfeodrippe/jank-examples --log-failed`
- Re-run CI: Push changes to trigger new run

## Next Steps
Commit and push to jank-examples develop branch to fix CI.
