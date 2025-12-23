# iOS Remote Eval - Usage Guide

## Overview

jank on iOS uses a ClojureScript-style architecture to avoid stack overflow from heavy nREPL template instantiation:

- **iOS device**: Runs a minimal eval server (BSD sockets, JSON protocol)
- **macOS**: Runs full nREPL server, forwards eval requests to iOS via TCP

## TL;DR - Quick Start

```bash
# Terminal 1: USB port forwarding
iproxy 5559 5558

# Terminal 2: jank nREPL server
cd /Users/pfeodrippe/dev/jank/compiler+runtime
./build/jank nrepl-server --port 5557
```

```elisp
;; Emacs: Load helper, connect CIDER, then connect to iOS
M-x load-file RET /Users/pfeodrippe/dev/jank/compiler+runtime/tools/ios-eval.el RET
M-x cider-connect RET localhost RET 5557 RET
M-x ios-eval-cider-connect RET localhost RET 5559 RET

;; Now C-c C-e, C-c C-c, etc. all eval on iPad!
```

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

### 3. Connect from Emacs via CIDER (Recommended)

This integrates with CIDER so all eval commands (`C-c C-e`, `C-c C-c`, etc.) go to iPad!

1. Start jank nREPL server on macOS:
   ```bash
   cd /Users/pfeodrippe/dev/jank/compiler+runtime
   ./build/jank nrepl-server --port 5557
   ```

2. Load the iOS eval helper:
   ```
   M-x load-file RET /Users/pfeodrippe/dev/jank/compiler+runtime/tools/ios-eval.el RET
   ```

3. Connect CIDER to jank nREPL:
   ```
   M-x cider-connect RET localhost RET 5557 RET
   ```

4. Connect to iOS device:
   ```
   M-x ios-eval-cider-connect RET localhost RET 5559 RET
   ```
   You'll see **[iOS]** in your mode-line when connected.

5. Now all CIDER evals go to iPad:
   - `C-c C-e` - eval last sexp
   - `C-c C-c` - eval defun
   - `C-x C-e` - eval last sexp
   - All evaluated on iPad!

6. Other commands:
   - `M-x ios-eval-cider-disconnect` - stop forwarding, eval back to macOS
   - `M-x ios-eval-cider-status` - check connection status
   - `M-x ios-eval-cider-toggle` - toggle iOS connection

### 4. Direct iOS Eval (Standalone, without CIDER)

For simple eval without full CIDER integration:

1. Load the iOS eval helper:
   ```
   M-x load-file RET /Users/pfeodrippe/dev/jank/compiler+runtime/tools/ios-eval.el RET
   ```

2. Connect directly to iOS:
   ```
   M-x ios-eval-connect RET localhost RET 5559 RET
   ```

3. Use standalone keybindings:
   - `C-c C-i` - eval last sexp on iOS
   - `C-c C-d` - eval defun on iOS
   - `C-c C-r` - eval region on iOS
   - `C-c C-b` - eval buffer on iOS

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
