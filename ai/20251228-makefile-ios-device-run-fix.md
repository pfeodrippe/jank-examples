# Makefile iOS Device Run Fix

## Problem 1: Compile server not starting
When running `make ios-jit-only-device-run`, the compile server on port 5571 wasn't starting.

### Root Cause
The target dependencies were ordered:
```makefile
ios-jit-only-device-run: ios-jit-only-device-build ios-compile-server-device ios-device-nrepl-proxy
```
The `ios-jit-only-device-build` step was failing (due to code signing), so Make stopped before reaching the compile server target.

### Fix
Reordered dependencies to start services first:
```makefile
ios-jit-only-device-run: ios-compile-server-device ios-device-nrepl-proxy ios-jit-only-device-build
```

## Problem 2: Code signing failing from CLI
`xcodebuild` was showing "No Accounts" error even though Apple ID was configured in Xcode.

### Root Cause
Wrong team ID in project YAML files:
- Was: `937QJHK26U` (certificate ID)
- Should be: `GNRTKYX7C4` (team ID from Xcode account)

### How to find your team ID
```bash
defaults read com.apple.dt.Xcode | grep -A5 "teamID"
```

### Fix
Updated all project YAML files:
```bash
sed -i '' 's/937QJHK26U/GNRTKYX7C4/g' SdfViewerMobile/project*.yml
```

## Result
CLI deployment now works without opening Xcode:
```bash
make ios-jit-only-device-run
```

## Commands Used
- `lsof -i :5571` - Check if compile server is listening
- `security find-identity -v -p codesigning` - List signing certificates
- `defaults read com.apple.dt.Xcode | grep teamID` - Find your team ID

## Normal Warnings (Not Errors)
These warnings appear during JIT compilation and are expected:
- `Warning: Could not find source for: clojure.core-native` - Native modules built into runtime
- `Warning: Could not find source for: jank.perf-native` - Same
- `Warning: Could not find source for: cpp` - Same
- `WARNING: 'run!' already referred to...` - Clojure var redefinition (normal)
