# GitHub CI Workflow Setup

**Date:** 2025-12-13

## Goal

Add GitHub Actions CI workflow to:
1. Run `make test`
2. Run `make sdf-standalone`
3. Upload the DMG as a downloadable artifact

## Implementation

### Created `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main, master, develop]
  pull_request:
    branches: [main, master, develop]
  workflow_dispatch:

env:
  JANK_REPO: pfeodrippe/jank
  JANK_REF: nrepl  # Must match local jank branch!
```

**Key features:**
- Runs on `macos-14` (Apple Silicon M1/M2/M3)
- Installs homebrew dependencies (openssl, zstd, SDL3, vulkan, shaderc, molten-vk, llvm)
- Clones and builds jank from `pfeodrippe/jank` `nrepl` branch (with caching)
- Updates hardcoded paths in scripts for CI environment
- Runs `make test`
- Runs `make sdf-standalone`
- Uploads `SDFViewer.dmg` as artifact

### Updated `Makefile`

Added `test` as alias for `tests` target:
```makefile
test tests:
	./bin/run_tests.sh
```

## Files Created/Modified

- `.github/workflows/ci.yml` - New CI workflow
- `Makefile` - Added `test` target alias

## Workflow Steps

1. **Checkout** - Clone repo with submodules
2. **Install deps** - Homebrew packages
3. **Cache jank** - Speeds up subsequent runs
4. **Build jank** - Only if not cached
5. **Update paths** - Replace hardcoded `/Users/pfeodrippe/dev/...` paths
6. **Build deps** - ImGui, Jolt, Flecs
7. **Run tests** - `make test`
8. **Build standalone** - `make sdf-standalone`
9. **Upload artifact** - `SDFViewer.dmg`

## Artifact Output

After successful CI run, the DMG will be available at:
`Actions > [workflow run] > Artifacts > SDFViewer-macOS-arm64`

## Notes

- First run takes ~1-2 hours to build LLVM/Clang (cached for subsequent runs)
- Uses `nrepl` branch of jank from `pfeodrippe/jank` (same as local development)
- Standalone DMG only built on macOS
- Linux runs tests but no standalone build (would need AppImage or similar)

## Platform Support

| Platform | Tests | Standalone |
|----------|-------|------------|
| macOS (arm64) | ✓ | ✓ DMG |
| Ubuntu 24.04 | ✓ | - |

## Commands Used

```bash
# Check local jank branch
cd /Users/pfeodrippe/dev/jank && git branch --show-current
# Output: nrepl

# Check CI status
gh run list --limit 5
gh run view <run-id> --log-failed
```
