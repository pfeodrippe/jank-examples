# Fix make sdf-clean CLI flags

## Problem
`make sdf-clean` was failing with `error: Invalid command '--framework'.`

## Root Cause
The jank CLI on `nrepl-4` branch was missing several options that `run_sdf.sh` expected:
- `--framework` - macOS frameworks
- `--obj` - object files
- `--lib` - libraries (only `-l`/`--link` existed)
- `--jit-lib` - JIT-only libraries
- `--link-lib` - AOT linker-only libraries

These were added in commit `d4d944f01 lib-jit` but that commit used CLI11's `add_option()` syntax. The current jank was refactored to use a manual CLI parser with `check_flag()`, so the options weren't migrated.

## Fixes Applied

### 1. Updated help text in jank (`src/cpp/jank/util/cli.cpp`)

Changed `-l` to `-l, --lib` and added new options documentation.

### 2. Added CLI parsing in jank (`src/cpp/jank/util/cli.cpp`)

```cpp
else if(check_flag(it, end, value, "-l", "--lib", true))
{
  opts.libs.emplace_back(value);
}
else if(check_flag(it, end, value, "--obj", true))
{
  opts.object_files.emplace_back(value);
}
else if(check_flag(it, end, value, "--framework", true))
{
  opts.frameworks.emplace_back(value);
}
else if(check_flag(it, end, value, "--jit-lib", true))
{
  opts.jit_libs.emplace_back(value);
}
else if(check_flag(it, end, value, "--link-lib", true))
{
  opts.link_libs.emplace_back(value);
}
```

### 3. Removed redundant `-main` flag from `bin/run_sdf.sh`

The `run-main` command already executes `-main` automatically, so the explicit `-main` flag on line 837 was causing `error: Extra positional args: -main`.

## Commands
```bash
# Rebuild jank after changes
cd /Users/pfeodrippe/dev/jank/compiler+runtime
ninja -C build

# Test
make sdf-clean
```

## Status
The CLI flags are now working. A separate `jtl::option<void>` compiler issue appeared during JIT compilation, which is unrelated to this fix.
