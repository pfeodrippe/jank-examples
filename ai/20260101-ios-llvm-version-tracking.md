# iOS LLVM Version Tracking Fix

## Problem
The iOS JIT simulator was crashing with "malformed block record in AST file" error when loading the PCH (precompiled header). This was caused by LLVM version mismatch:
- iOS LLVM was built from commit `4e5928689` (Dec 22)
- macOS jank LLVM was at commit `a8f19259e` (current)

The PCH was built with the newer macOS clang but the iOS app uses older iOS LLVM, causing incompatible PCH format.

## Solution

### 1. Rebuild iOS LLVM
Started rebuild to match current jank LLVM commit:
```bash
make ios-jit-llvm-sim
```
This takes ~2 hours.

### 2. Added Version Tracking to Makefile
Added automatic version tracking to prevent this issue in the future:

- **ios-jit-llvm-sim** and **ios-jit-llvm-device** now save the LLVM commit hash to `.commit` files after building
- **check-ios-llvm-version-sim** and **check-ios-llvm-version-device** targets verify the commits match
- **ios-jit-sim** now depends on `check-ios-llvm-version-sim` to fail fast if versions mismatch

The commit files are stored at:
- `~/dev/ios-llvm-build/ios-llvm-simulator.commit`
- `~/dev/ios-llvm-build/ios-llvm-device.commit`

## Commands Used
```bash
# Start iOS LLVM rebuild
make ios-jit-llvm-sim

# Check LLVM commits
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm && git rev-parse HEAD
```

## Next Steps
1. Wait for iOS LLVM rebuild to complete
2. Run `make ios-jit-sim-run` to test the full flow
3. Verify the app runs without PCH corruption crash

## Key Files Modified
- `/Users/pfeodrippe/dev/something/Makefile` - Added version tracking targets
