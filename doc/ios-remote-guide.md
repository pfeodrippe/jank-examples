# iOS Remote Eval - Usage Guide

## Overview

jank on iOS uses a ClojureScript-style architecture to avoid stack overflow from heavy nREPL template instantiation:

- **iOS device**: Runs a minimal eval server (BSD sockets, JSON protocol)
- **macOS**: Runs full nREPL server, forwards eval requests to iOS via TCP

## Prerequisites

1. **iproxy** for USB port forwarding:
   ```bash
   brew install libimobiledevice
   ```

2. **iPad connected via USB** and app running with eval server

## Quick Start

### 1. Start USB Port Forwarding

```bash
iproxy 5559 5558
```

This forwards iPad's port 5558 to localhost:5559.

### 2. Test Direct Connection (Optional)

```bash
# Ping
(echo '{"op":"ping","id":1}'; sleep 1) | nc localhost 5559

# Eval
(echo '{"op":"eval","id":1,"code":"(+ 1 2)"}'; sleep 1) | nc localhost 5559
# Returns: {"op":"result","id":1,"value":"3"}
```

### 3. Connect from Emacs via nREPL

**Option A: Using jank's nREPL with iOS forwarding**

1. Start jank nREPL server on macOS:
   ```bash
   cd /Users/pfeodrippe/dev/jank/compiler+runtime
   ./build/jank nrepl-server --port 5557
   ```

2. Connect Emacs to jank nREPL:
   ```
   M-x cider-connect RET localhost RET 5557 RET
   ```

3. Connect to iOS device (in REPL buffer):
   ```clojure
   (nrepl/ios-connect "localhost" 5559)
   ```

4. Now all evals go to iPad:
   ```clojure
   (+ 1 2)  ; Evaluated on iPad!
   (println "Hello from iPad")
   ```

5. Disconnect when done:
   ```clojure
   (nrepl/ios-disconnect)
   ```

**Option B: Direct iOS eval from Emacs**

1. Load the iOS eval helper:
   ```
   M-x load-file RET /Users/pfeodrippe/dev/jank/compiler+runtime/tools/ios-eval.el RET
   ```

2. Connect:
   ```
   M-x ios-eval-connect RET localhost RET 5559 RET
   ```

3. Evaluate:
   ```
   M-x ios-eval RET (+ 1 2) RET
   ```

## Protocol

JSON over TCP, newline-delimited:

```
Client → Server: {"op":"eval","id":1,"code":"(+ 1 2)","ns":"user"}
Server → Client: {"op":"result","id":1,"value":"3"}
Server → Client: {"op":"error","id":1,"error":"...","type":"compile|runtime"}
```

Operations:
- `ping` - Health check, returns `pong`
- `eval` - Evaluate code, returns `result` or `error`
- `shutdown` - Stop the eval server

## Live Code Reloading Example

To redefine a function on the running iPad app:

1. Switch to the correct namespace:
   ```bash
   (echo '{"op":"eval","id":1,"code":"(in-ns (quote vybe.sdf.ui))"}'; sleep 1) | nc localhost 5559
   ```

2. Eval the modified function:
   ```bash
   # Save your jank code to a file, then:
   CODE=$(cat /tmp/my_function.jank | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n' ' ')
   (echo "{\"op\":\"eval\",\"id\":2,\"code\":\"$CODE\"}"; sleep 3) | nc localhost 5559
   ```

3. The function is immediately replaced - no rebuild needed!

## JIT vs AOT Differences

When evaling code via the iOS eval server (JIT), some things work differently than AOT:

### `cpp/unbox` requires explicit types
AOT can infer pointer types from var metadata, but JIT cannot:
```clojure
;; AOT - works (type inferred from var)
(cpp/unbox *my-bool-ptr)

;; JIT - fails with "Unable to infer type"
(cpp/unbox *my-bool-ptr)

;; JIT - works (explicit type)
(cpp/unbox "bool" *my-bool-ptr)
```

**Workaround**: For JIT-compatible code, either:
- Add explicit types: `(cpp/unbox "bool" ptr)`, `(cpp/unbox "float" ptr)`
- Or avoid `cpp/unbox` in hot-reloaded functions

### Namespace context
JIT eval starts in `user` namespace. Switch first:
```clojure
(in-ns 'your.namespace)
```

## Troubleshooting

### "No route to host"
- Ensure iPad is connected via USB
- Run `iproxy 5559 5558` to forward the port
- Connect to `localhost:5559`, not the iPad's IP directly

### Connection refused
- Make sure the iOS app is running
- Check Xcode console for `[ios-eval] Listening on 0.0.0.0:5558`

### Eval errors
- Check the error type: `compile` vs `runtime`
- Avoid special characters that need JSON escaping

### SIGABRT in GC_push_all_stacks or GC_register_my_thread
The eval server thread must be registered with Boehm GC. This requires:
1. `GC_allow_register_threads()` called from main thread before spawning
2. `GC_register_my_thread()` at start of server thread
3. `GC_unregister_my_thread()` before thread exits

See `eval_server.hpp` for the implementation.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         macOS                                │
│  ┌─────────────┐     ┌─────────────┐     ┌──────────────┐  │
│  │   Emacs     │────▶│ jank nREPL  │────▶│   iproxy     │  │
│  │   CIDER     │     │   Server    │     │ 5559 → 5558  │  │
│  └─────────────┘     └─────────────┘     └──────┬───────┘  │
└─────────────────────────────────────────────────┼──────────┘
                                                  │ USB
┌─────────────────────────────────────────────────┼──────────┐
│                         iPad                    │          │
│  ┌──────────────────────────────────────────────▼───────┐  │
│  │              iOS Eval Server (port 5558)             │  │
│  │                                                      │  │
│  │   • Receives JSON eval requests                      │  │
│  │   • Calls jank's eval_string()                       │  │
│  │   • Returns results as JSON                          │  │
│  │   • Signal recovery for crashes                      │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Files

- `include/cpp/jank/ios/eval_server.hpp` - iOS eval server (BSD sockets)
- `include/cpp/jank/ios/eval_client.hpp` - macOS client for connecting to iOS
- `include/cpp/jank/nrepl_server/ios_remote_eval.hpp` - nREPL utility functions
- `include/cpp/jank/nrepl_server/ops/ios_eval.hpp` - nREPL operation handlers
- `tools/ios-eval.el` - Emacs integration for direct iOS eval

## Technical Notes

### Boehm GC Thread Registration

The eval server runs on a separate pthread with 8MB stack. For GC to work correctly during eval, the thread must be registered:

```cpp
// In start() - main thread, before pthread_create:
GC_allow_register_threads();

// In run_server() - eval thread, at the beginning:
struct GC_stack_base sb;
GC_get_stack_base(&sb);
GC_register_my_thread(&sb);

// At thread exit:
GC_unregister_my_thread();
```

Without this, any GC during eval will crash with SIGABRT in `GC_push_all_stacks`.

### Why BSD Sockets Instead of Boost.Asio

iOS doesn't include Boost.Asio in the jank bundle, and adding it would significantly increase binary size. BSD sockets are available on all Apple platforms and work reliably for this simple TCP server use case.
