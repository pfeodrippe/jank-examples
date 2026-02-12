# Project Rules

Rule 0 - **NEVER edit derived/generated files directly!**
- **NEVER** edit files in `*/jank-resources/*` - these are copied from `src/`
- **NEVER** edit files in `*/build-*/*` or `*/generated/*`
- Always find and edit the **original source** file (usually in `src/`)
- Use `find . -name "filename" | grep -v jank-resources | grep -v build` to find originals
- If `git status` shows "nothing to commit" after your edit, you edited the wrong file!

Rule 1 - In the end of your turn, create a new .md file into the `ai` folder with what you've learned + commands you did + what you will do next!

Rule 2 - **Use jank/cpp prefix, cpp/raw is LAST RESORT**
- Always prefer header requires: `["raylib.h" :as rl :scope ""]`
- Use `cpp/` prefix for C++ interop: `cpp/box`, `cpp/unbox`, `cpp/.-field`, `cpp/.method`
- Use `let*` with `_` binding for void returns
- Only use `cpp/raw` when absolutely necessary (complex loops, callbacks, templates, ODR-safe globals)
- Check `ai/20251202-native-resources-guide.md` for complete cpp/ API

Rule 3 - DON'T do one-off commands without also improving the makefile script!!!

Rule 4 - For commands that may require further inspection, use tee to append to also put the output into a file!!

## Linking

- **ALWAYS use static linking** - Never use dynamic libraries (.dylib, .so)
- Use object files (.o) or static libraries (.a) that can be loaded by the JIT
- For jank integration, compile C/C++ code to object files that can be loaded directly

## jank Compiler Fixes

When you need to fix issues in the jank compiler itself (not WASM workarounds):

**Location**: `/Users/pfeodrippe/dev/jank/compiler+runtime/`

**Build Environment Setup** (required before building on macOS):
```bash
cd /Users/pfeodrippe/dev/jank/compiler+runtime
# IMPORTANT: Use the actual expanded path, NOT $(xcrun --show-sdk-path) which may not expand in some shells
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export CC=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang
export CXX=/Users/pfeodrippe/dev/jank/compiler+runtime/build/llvm-install/usr/local/bin/clang++
```

**Build Commands**:
```bash
# Build
./bin/compile

# Run tests
./bin/test
```

**Key Files**:
- `src/cpp/jank/runtime/context.cpp` - WASM AOT code generation (includes, codegen)
- `include/cpp/jank/runtime/` - Runtime headers
- `AGENTS_CONTEXT.md` - C API exports for WASM (jank_set_meta, etc.)

**When to fix in jank vs workaround**:
- **Fix in jank**: Missing includes in generated code, missing C API exports, compiler bugs
- **Workaround in demo**: Demo-specific logic, one-off cases that don't affect other users
- I've restarted it, but don't use :reload-all
- we want to put as many things as possibles in jank!!

## Command Output Best Practices

Rule 10 - **Use tee for commands with potential failures**
- When running commands that may produce errors, use `2>&1 | tee /tmp/build_output.txt` to save output
- This avoids running slow commands twice just to see the full error output
- Example: `make ios-aot-sim-run 2>&1 | tee /tmp/ios_build.txt`
- Then read the file with `Read` tool or `cat /tmp/ios_build.txt` if needed

Rule 11 - **Check long-running commands every 60 seconds**
- NEVER set timeout > 60 seconds for build commands
- Use `block: false` or `timeout: 60000` and check periodically
- Check status, if still running wait another 60s, repeat until done
- This allows user to see progress and interrupt if needed

## iPad RoughAnimator Access (pymobiledevice3)

Use these commands to inspect and pull `com.weirdhat.roughanimator` files from iPad (USB or Wi-Fi).

### Prerequisites

```bash
# iPad must be unlocked and trusted by this Mac.
# For Wi-Fi only, enable Finder option: "Show this iPad when on Wi-Fi" once via USB.
pymobiledevice3 usbmux list
```

If multiple devices appear, set UDID first:

```bash
export IOS_UDID="<from pymobiledevice3 usbmux list>"
```

### List RoughAnimator projects

```bash
export RA_BUNDLE=com.weirdhat.roughanimator
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
ls
exit
EOF
```

If `IOS_UDID` is not set, remove `--udid "$IOS_UDID"`.

### Inspect a project and verify frame timestamps

```bash
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
cd "voiture.ra"
cat data.txt
stat 0003/0000.png
exit
EOF
```

### Pull the full project locally

```bash
mkdir -p /Users/pfeodrippe/dev/something/roughanimator_from_ipad
pymobiledevice3 apps afc --documents --udid "$IOS_UDID" "$RA_BUNDLE" <<'EOF'
pull "voiture.ra" "/Users/pfeodrippe/dev/something/roughanimator_from_ipad"
exit
EOF
```

### Pull a single file quickly

```bash
pymobiledevice3 apps pull --udid "$IOS_UDID" "$RA_BUNDLE" \
  "voiture.ra/data.txt" \
  "/tmp/voiture_data.txt"
```
