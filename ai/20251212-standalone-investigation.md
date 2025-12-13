# Session: Standalone App Investigation

## Date: 2025-12-12

## Question
User asked: "Are we using jank-standalone when creating the standalone app?"

## Investigation

### Key Findings

1. **No separate `jank-standalone` executable exists** - The jank build produces:
   - `jank` (symlink to `jank-phase-1`) - The main compiler/runtime executable
   - `libjank-standalone.a` (~460MB) - Merged static library containing all jank dependencies

2. **How standalone compilation works**:
   - `jank compile -o <output> <module>` - AOT compiles jank code to a standalone executable
   - The `compile` subcommand links against `libjank-standalone.a`
   - Options: `--runtime {dynamic,static}` (default: dynamic)

3. **In `bin/run_sdf.sh`**:
   - Uses `jank compile -o "$OUTPUT_NAME" vybe.sdf` (line 484)
   - Then wraps in macOS app bundle with `create_macos_app_bundle()`
   - Bundles: dylibs, headers, clang for JIT, Vulkan ICD, shaders, etc.

### jank Build Artifacts

```
/Users/pfeodrippe/dev/jank/compiler+runtime/build/
├── jank -> jank-phase-1           # Main executable (symlink)
├── jank-phase-1                   # Compiler/runtime (44MB)
├── libjank.a                      # Core jank library (347MB)
├── libjank-standalone-phase-1.a   # Phase 1 merged lib (437MB)
├── libjank-standalone.a           # Final merged lib (460MB)
└── jank-test                      # Test executable
```

### CMakeLists.txt Notes

From `/Users/pfeodrippe/dev/jank/compiler+runtime/CMakeLists.txt`:
- `libjank-standalone.a` merges: jank_lib, jankzip_lib, bdwgc, folly, cppinterop, etc.
- Uses custom `ar-merge` script to combine static libraries
- Two phases: phase-1 (before core libs), phase-2 (with core libs)

## Commands Used

```bash
# Check jank compile help
jank compile --help

# Build standalone
./bin/run_sdf.sh --standalone

# Test in sandbox
./bin/test-standalone-sandbox.sh SDFViewer.app
```

## Next Steps

None - this was an informational query.
