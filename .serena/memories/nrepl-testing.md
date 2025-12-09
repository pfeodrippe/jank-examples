# nREPL Testing Guide

## Connection

The project has an nREPL server running on port 5557 (check `.nrepl-port` file).

## Running Tests via nREPL

### Run all tests in a namespace
```bash
clj-nrepl-eval -p 5557 "(require 'vybe.flecs-test) (clojure.test/run-tests 'vybe.flecs-test)"
```

### Run a specific test
```bash
clj-nrepl-eval -p 5557 "(clojure.test/test-vars [#'vybe.flecs-test/entity-update-test])"
```

### Reload a namespace after changes
```bash
# Use :reload for a single namespace
clj-nrepl-eval -p 5557 "(require 'vybe.type :reload)"

# For dependent namespaces, reload in order
clj-nrepl-eval -p 5557 "(require 'vybe.type :reload) (require 'vybe.flecs :reload)"
```

### Evaluate arbitrary code
```bash
clj-nrepl-eval -p 5557 "(println :AAA)"
```

## Test Location

- Tests are in `test/vybe/flecs_test.jank`
- Uses `deftest` from clojure.test
- Example tests: `entity-update-test`, and 15 other tests

## Discovering nREPL Ports

```bash
clj-nrepl-eval --discover-ports
```

## Important Notes

- Use `defonce` for atoms and mutable state to preserve state across reloads
- To fully reload a file, use `cat file.jank | clj-nrepl-eval -p PORT`
- Simple `(require 'ns :reload)` may not re-execute `def` forms
- Components defined with `defcomp` are namespace-qualified
- Keywords used as tags are mangled (`:my-tag` -> `"my_tag"`, `:foo/bar` -> `"foo__bar"`)