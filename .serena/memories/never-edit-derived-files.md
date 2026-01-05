# Never Edit Derived/Generated Files

## Critical Rule

**NEVER edit files in these directories directly:**
- `*/jank-resources/*` - These are copied/synced from source files
- `*/build-*/*` - Build output directories
- `*/generated/*` - Generated code
- `DerivedData/*` - Xcode build artifacts

## Why?

These files are:
1. **Overwritten during build** - Changes will be lost
2. **Not git-tracked** - Changes won't be committed
3. **Derived from source** - The source file is the single source of truth

## What to Do Instead

1. Find the **original source file**:
   ```bash
   find /Users/pfeodrippe/dev/something -name "filename.hpp" -type f | grep -v jank-resources | grep -v build
   ```

2. Edit the **original** file (usually in `src/` directory)

3. The build process will copy/sync the changes to derived locations

## Example

- **WRONG**: `DrawingMobile/jank-resources/src/jank/vybe/app/drawing/native/drawing_canvas.hpp`
- **RIGHT**: `src/vybe/app/drawing/native/drawing_canvas.hpp`

## How to Verify

After editing, run `git status` or `git diff` - if your file shows as modified, you edited the right file.
If it shows "nothing to commit, working tree clean", you edited a derived file.
