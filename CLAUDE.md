# Project Rules

Rule 1 - In the end of your turn, create a new .md file into the `ai` folder with what you've learned + commands you did + what you will do next!

Rule 2 - **Use jank/cpp prefix, cpp/raw is LAST RESORT**
- Always prefer header requires: `["raylib.h" :as rl :scope ""]`
- Use `cpp/` prefix for C++ interop: `cpp/box`, `cpp/unbox`, `cpp/.-field`, `cpp/.method`
- Use `let*` with `_` binding for void returns
- Only use `cpp/raw` when absolutely necessary (complex loops, callbacks, templates, ODR-safe globals)
- Check `ai/20251202-native-resources-guide.md` for complete cpp/ API

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
export SDKROOT=$(xcrun --show-sdk-path)
export CC=$PWD/build/llvm-install/usr/local/bin/clang
export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
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