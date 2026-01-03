# C++ Debug Formatting

When inspecting generated jank C++ code, always format it first:

```bash
# Format before reading
clang-format /tmp/jank-debug-dep-module_name.cpp > /tmp/jank-debug-dep-module_name-formatted.cpp
```

This makes the code much easier to read since the unformatted code puts everything on one line.
