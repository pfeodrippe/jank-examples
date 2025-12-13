# sandbox-exec Implementation for Standalone App Testing

**Date**: 2025-12-12

## What I Learned

### macOS sandbox-exec
- `sandbox-exec -f profile.sb command` runs a process in a sandbox
- Profiles use Scheme-like syntax with `(version 1)` header
- Two approaches: deny-by-default (secure) or allow-by-default (permissive)
- Resource targeting: `literal`, `regex`, `subpath` for paths
- Debug denials with: `/bin/bash -c 'log show --predicate "sender == \"Sandbox\"" --last 1m'`

### Key Sandbox Rules Used
- `(deny default)` - start locked down
- `(allow file-read-metadata)` - needed for `std::filesystem::canonical`
- `(allow file-read* (literal "/"))` - needed for path resolution
- `(allow mach-lookup)` - required for most macOS apps
- `(allow iokit-open)` - GPU/graphics access
- `(deny network*)` - block all network

### SDFViewer.app Test Results
- **Requires Xcode** - not truly standalone, uses `xcrun` for JIT compilation
- **Tries to bind network** - nREPL server on port 5557, correctly blocked
- **Does NOT need homebrew** - `/opt/homebrew` blocked without issues
- **Works with `--allow-xcode`** - app runs fully, saves screenshot, clean exit

## Commands Executed

1. Read `bin/test-standalone-sandbox.sh` - understand current implementation
2. Fetched https://igorstechnoclub.com/sandbox-exec/ - learn sandbox-exec
3. Iteratively fixed sandbox profile through testing:
   - Added `file-read-metadata` for filesystem::canonical
   - Added `/` literal for path resolution
   - Added tty devices for terminal output
   - Made Xcode and network rules conditional

## Final Script Features

```bash
./bin/test-standalone-sandbox.sh [options] <app-path>

Options:
  --allow-xcode    Allow Xcode/CommandLineTools (for JIT apps)
  --allow-network  Allow network access
```

### Sandbox Rules Summary
- **Always denied**: `/opt/homebrew`, `/usr/local`, `/nix`, `~/.jank`, `~/.clojure`, `~/.m2`
- **Conditionally denied**: Xcode, network (controlled by flags)
- **Always allowed**: System libraries, app bundle, tmp, GPU/graphics

## Key Findings

1. **SDFViewer requires SDK headers** - needs Xcode OR CommandLineTools for JIT compilation
   - Most end-user Macs have CommandLineTools installed (via `xcode-select --install` or git prompt)
   - This is why it works on wife's computer - she has CommandLineTools, not full Xcode
2. **App does NOT need homebrew** - `/opt/homebrew`, `/usr/local` blocked without issues
3. **App gracefully degrades** - nREPL network bind fails, but app continues
4. **App does NOT need dev home directories** - `~/.jank`, `~/.clojure`, `~/.m2` blocked

## Final Usage

```bash
# Default: Simulates typical end-user Mac (has CommandLineTools)
./bin/test-standalone-sandbox.sh SDFViewer.app

# Strict: Bare Mac without CommandLineTools (will fail for JIT apps)
./bin/test-standalone-sandbox.sh --no-sdk SDFViewer.app

# Also allow network if needed
./bin/test-standalone-sandbox.sh --allow-network MyApp.app
```

## What "Standalone" Means for jank Apps

A jank app is "standalone" if it works on a Mac with:
- CommandLineTools installed (provides SDK headers for JIT) ✓
- No homebrew ✓
- No ~/.jank, ~/.clojure, ~/.m2 directories ✓
- No network (optional, app should degrade gracefully) ✓

This matches most end-user Macs that have ever used git or any dev tool.

## Issue Discovered: Hardcoded Build Paths

The sandbox testing revealed that the bundled clang has **hardcoded paths** to the build system:
```
/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/../include/c++/v1/new
```

This means the app currently requires access to `$HOME` to read these files. On another user's machine, these paths won't exist and the app may fail. This should be fixed in the jank bundling process.
