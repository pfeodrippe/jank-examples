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
