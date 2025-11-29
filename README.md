# Flecs + jank Integration Examples

## Setup

```shell
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:$PATH"
```

## Build Flecs (one-time)

```shell
cd vendor/flecs/distr
clang -shared -fPIC -o libflecs.dylib flecs.c
```

## Run Examples

### Flecs C API (minimal wrapper)

Uses `FLECS_NO_CPP` with thin jank wrappers calling C functions directly.

```shell
jank -I./vendor/flecs/distr \
     -l/Users/pfeodrippe/dev/something/vendor/flecs/distr/libflecs.dylib \
     --module-path src \
     run-main my-flecs -main
```

### Flecs C++ API (full power with components & systems)

Uses the full C++ API with templates, type-safe components, and lambda systems.

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

- `src/my_flecs.jank` - C API example (minimal boilerplate)
- `src/my_flecs_cpp.jank` - C++ API example (components, systems, lambdas)
- `vendor/flecs/distr/` - Flecs source and compiled library

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
