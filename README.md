# Flecs + jank Integration Examples

## Setup

```shell
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH"
```

## Build Flecs (one-time)

```shell
cd vendor/flecs/distr
# Object file (recommended - no dynamic library needed!)
clang -c -fPIC -o flecs.o flecs.c

# Or dynamic library (alternative)
clang -shared -fPIC -o libflecs.dylib flecs.c
```

## Run Examples

### Static Object File (RECOMMENDED - no -l flag needed!)

Loads flecs.o directly into the JIT via `jit_prc.load_object()`. No dynamic library required!

```shell
jank -I./vendor/flecs/distr \
     --module-path src \
     run-main my-flecs-static -main
```

### Dynamic Library (alternative)

Uses `-l` to load the dylib via dlopen.

```shell
jank -I./vendor/flecs/distr \
     -l/Users/pfeodrippe/dev/something/vendor/flecs/distr/libflecs.dylib \
     --module-path src \
     run-main my-flecs-cpp -main
```

Expected output:
```
=== Flecs C++ ECS Demo ===
Static entity: 578
  Position: {:x 10.000000, :y 20.000000}

Moving entity: 579
  Initial position: {:x 0.000000, :y 0.000000}

Running simulation...
  Position: {:x 1.000000, :y 2.000000}
  Position: {:x 2.000000, :y 4.000000}
  Position: {:x 3.000000, :y 6.000000}
  Position: {:x 4.000000, :y 8.000000}
  Position: {:x 5.000000, :y 10.000000}

Done!
```

## Files

- `src/my_flecs_static.jank` - **RECOMMENDED**: Loads .o file directly into JIT (no dylib!)
- `src/my_flecs.jank` - C API example with dylib (minimal boilerplate)
- `src/my_flecs_cpp.jank` - C++ API example with dylib (components, systems, lambdas)
- `vendor/flecs/distr/` - Flecs source and compiled files

## Others

``` shell
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" jank --module-path src run-main eita
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" jank -Ivendor/flecs/distr -lvendor/flecs/distr/libflecs.dylib --module-path src run-main my-example start-server
PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH" llbd jank -I./vendor/flecs/distr --module-path src run-main my-example start-server
```

``` shell
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH"
jank -I./vendor/flecs/distr \
      -l/Users/pfeodrippe/dev/something/vendor/flecs/distr/libflecs.dylib \
      --module-path src \
      run-main my-flecs -main
```
