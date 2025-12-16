# Object File Sizes Investigation - Debug Mode

## Summary

The object files in `target/` are large because jank is compiled in Debug mode.

## Findings

### Object File Sizes (from `make sdf-standalone`)

| File | Size |
|------|------|
| vybe/sdf/ui.o | 5.2 MB |
| vybe/flecs.o | 3.1 MB |
| vybe/sdf/render.o | 2.6 MB |
| vybe/type.o | 2.5 MB |
| vybe/sdf/math.o | 1.8 MB |
| vybe/sdf/shader.o | 1.8 MB |
| vybe/sdf/screenshot.o | 1.6 MB |
| vybe/sdf/events.o | 1.4 MB |
| vybe/util.o | 1.3 MB |
| vybe/sdf/state.o | 1.1 MB |
| clojure/string.o | 1.0 MB |
| vybe/sdf.o | 943 KB |

### Root Cause: jank Debug Mode

jank is built with `CMAKE_BUILD_TYPE=Debug`, which bakes these flags into `JANK_JIT_FLAGS`:

```
-O0 -g
```

- `-g`: Generates DWARF debug symbols
- `-O0`: No optimization (larger code, more symbols)

### Debug Section Breakdown (type.o example)

| Section | Size |
|---------|------|
| __debug_str | 929.7 KB |
| __debug_info | 311.5 KB |
| __debug_line_str | 112.1 KB |
| __debug_names | 94.4 KB |
| __debug_line | 63.4 KB |
| __debug_str_offs | 52.6 KB |
| __debug_addr | 25.2 KB |
| __debug_abbrev | 4.6 KB |
| __debug_rnglists | 0.2 KB |
| **Total Debug** | **~1.56 MB (62%)** |

### Solution Options

1. **Rebuild jank in Release mode:**
   ```bash
   cd /Users/pfeodrippe/dev/jank/compiler+runtime
   export SDKROOT=$(xcrun --show-sdk-path)
   export CC=$PWD/build/llvm-install/usr/local/bin/clang
   export CXX=$PWD/build/llvm-install/usr/local/bin/clang++
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ./bin/compile
   ```

2. **Strip debug symbols post-build:**
   ```bash
   strip -S target/arm64-*/*.o
   ```

3. **Use RelWithDebInfo** for some debug capability with optimization:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
   ```

## Commands Used

```bash
# List object files with sizes
find target -name "*.o" -exec ls -lah {} \; | sort -k5 -h

# Check section sizes
size target/.../vybe/type.o

# View DWARF sections
otool -l target/.../vybe/type.o | grep -E "sectname __debug"

# Check embedded JIT flags in jank binary
strings /Users/pfeodrippe/dev/jank/compiler+runtime/build/jank | grep "\-O0\|\-g "
```

## Next Steps

- Consider building jank in Release mode for production standalone builds
- Debug mode is useful for development/debugging but increases binary size significantly
