# iOS JIT Unbound Var Fix Plan

## Date: 2026-01-03

## Problem

When running the iOS JIT app, the `-main` function is unbound:
```
invalid call with 0 args to unbound@0x13a032840 for var #'vybe.sdf.ios/-main
```

The execution trace is empty because the error occurs before any JIT-compiled functions are called.

The log shows:
```
[compile-client] Required namespace successfully, 0 module(s)
[loader] Phase 2 - Executing 0 entry functions...
```

## Root Cause Analysis

Found in `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp` lines 799-802:

```cpp
// Check if already loaded
if(loaded_namespaces_.find(ns_name) != loaded_namespaces_.end())
{
  std::cout << "[compile-server] Namespace already loaded: " << ns_name << std::endl;
  return R"({"op":"required","id":)" + std::to_string(id) + R"(,"modules":[]})";
}
```

**The Problem**:
1. The compile server persists `loaded_namespaces_` across multiple iOS app launches
2. When the iOS app restarts (fresh JIT state), the compile server remembers the namespace
3. Server returns an empty modules array
4. iOS JIT never receives the compiled code
5. The var `vybe.sdf.ios/-main` exists but is unbound (no function definition loaded)

**Why this happens**:
- The compile server is a long-running process on macOS
- The iOS Simulator app is killed and restarted frequently
- The server's `loaded_namespaces_` cache is per-process, not per-client-session
- When iOS reconnects, it's a fresh JIT with no modules loaded, but the server thinks they're already sent

## Solution Options

### Option A: Clear cache on client connection (Recommended)
Add cache clearing when a new client connects:

```cpp
void handle_connection(tcp::socket socket)
{
  // Clear loaded namespaces cache for fresh client
  loaded_namespaces_.clear();

  // ... rest of connection handling
}
```

**Pros**: Simple, matches expected behavior (each iOS launch gets fresh modules)
**Cons**: If multiple iOS devices connect, they each clear each other's cache

### Option B: Per-client namespace tracking
Track loaded namespaces per client connection instead of globally:

```cpp
void handle_connection(tcp::socket socket)
{
  std::unordered_set<std::string> client_loaded_namespaces;
  // ... use client_loaded_namespaces instead of loaded_namespaces_
}
```

**Pros**: Supports multiple simultaneous clients correctly
**Cons**: More complex, requires refactoring

### Option C: Protocol-level cache invalidation
Add a "reset" message to the protocol that iOS sends on startup:

```cpp
else if(op == "reset")
{
  loaded_namespaces_.clear();
  return R"({"op":"reset-done","id":)" + std::to_string(id) + "}";
}
```

**Pros**: Explicit control, iOS decides when to reset
**Cons**: Requires protocol change, iOS app modification

### Option D: Always recompile
Remove the cache entirely - always return compiled modules:

**Pros**: Simplest fix
**Cons**: Performance penalty (recompiles every time)

## Recommended Fix

**Option A** is the simplest and most appropriate fix. Since we typically have one iOS simulator connecting to one compile server, clearing the cache on each connection makes sense.

## Implementation Steps

1. Edit `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
2. Add `loaded_namespaces_.clear()` at the start of `handle_connection()`
3. Rebuild jank: `cd /Users/pfeodrippe/dev/jank/compiler+runtime && ninja -C build-ios-sim-jit`
4. Restart the compile server: `make ios-compile-server`
5. Restart the iOS app: `make ios-jit-sim-run`

## Files to Modify

- `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/compile_server/server.hpp`
  - Line ~298: In `handle_connection()`, add cache clear

## Build Commands

```bash
# Rebuild jank compiler
cd /Users/pfeodrippe/dev/jank/compiler+runtime
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
export CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++
./bin/compile

# Rebuild iOS library
ninja -C build-ios-sim-jit

# Restart compile server and iOS app
cd /Users/pfeodrippe/dev/something
make ios-compile-server  # In one terminal
make ios-jit-sim-run     # In another terminal
```

## Verification

After the fix, the logs should show:
```
[compile-client] Required namespace successfully, N module(s)  # N > 0
[loader] Phase 2 - Executing N entry functions...
```

And the app should start correctly with `-main` being called successfully.

---

## UPDATE: Second Issue - "not a number: nil"

After fixing the unbound var issue, a new error appears:

```
not a number: nil
=== jank Execution Trace (most recent last) ===
  0: -main
  1: run!
  2: draw
  3: update-uniforms!
=== End Execution Trace ===
```

### Analysis

The error occurs in `update-uniforms!` when calling `jank::runtime::add(const_92717, dt)`:
- `const_92717` = 0.0 (the first operand in `(+ 0.0 dt)`)
- `dt` = should be 0.016 (passed from `draw` as `const_92973`)

### Generated Code Analysis

1. Constants are declared at namespace scope:
```cpp
namespace vybe::sdf::ios {
  jank::runtime::obj::real_ref  const_92717;  // 0.0
  jank::runtime::obj::real_ref  const_92973;  // 0.016
}
```

2. Constants are initialized in `jank_load_vybe_sdf_ios$loading__()`:
```cpp
new (&vybe::sdf::ios::const_92717) jank::runtime::obj::real_ref(
  jank::runtime::make_box<jank::runtime::obj::real>(static_cast<jank::f64>(0.000000)));
new (&vybe::sdf::ios::const_92973) jank::runtime::obj::real_ref(
  jank::runtime::make_box<jank::runtime::obj::real>(static_cast<jank::f64>(0.016000)));
```

3. The loading function then calls `ns_load` which binds the JIT functions to vars

### Possible Causes

1. **Module loading order**: If the namespace is loaded but the loading function isn't fully executed before `-main` is called
2. **Constant initialization timing**: If constants are accessed before placement new is called
3. **JIT code caching**: If old compiled code is using stale constant addresses

### Next Steps

1. Add debug logging to the loading function to verify constants are initialized
2. Check if the iOS loader properly waits for module initialization to complete
3. Investigate whether the compile server returns pre-compiled code that doesn't trigger re-initialization
