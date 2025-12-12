# Standalone Flag for run_sdf.sh

## Date: 2025-12-12

## What I Learned

### jank CLI Subcommands for Compilation
- `compile-module`: Compiles a module to intermediate form (object file), does NOT create an executable
- `compile`: Full AOT compilation that creates a standalone executable using `aot::processor`

From jank's `main.cpp`:
```cpp
static void compile()
{
  __rt_ctx->compile_module("clojure.core").expect_ok();
  __rt_ctx->compile_module(opts.target_module).expect_ok();
  jank::aot::processor const aot_prc{};
  aot_prc.compile(opts.target_module).expect_ok();  // Links into executable
}
```

### CLI Options for compile subcommand
- `-o <filename>`: Output executable name (default: "a.out")
- `--runtime <type>`: Either "dynamic" or "static"
- Module name is required as positional arg

## Commands Executed

1. Read `bin/run_sdf.sh` to understand current structure
2. Searched jank project for standalone/AOT compilation patterns
3. Read `/Users/pfeodrippe/dev/jank/compiler+runtime/include/cpp/jank/util/cli.hpp` - CLI options struct
4. Read `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/main.cpp` - main entry showing compile vs compile-module
5. Read `/Users/pfeodrippe/dev/jank/compiler+runtime/src/cpp/jank/util/cli.cpp` - CLI parsing

## Changes Made

Updated `bin/run_sdf.sh` to support:
- `--standalone` flag: When set, builds a standalone executable instead of running
- `-o|--output <name>`: Custom output filename (default: "sdf-viewer")

### Usage
```bash
# Run normally (JIT mode)
./bin/run_sdf.sh

# Build standalone executable
./bin/run_sdf.sh --standalone

# Build with custom output name
./bin/run_sdf.sh --standalone -o my-sdf-app
```

## What's Next

- Test the standalone build to verify it works correctly
- May need to add `--runtime static` option if dynamic linking causes issues
