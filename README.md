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

### WASM

For running in node, we can use something like

``` shell
cd /Users/pfeodrippe/dev/jank/compiler+runtime/build-wasm && node --experimental-vm-modules -e "import('./my_integrated_demo.js').then(m => m.default())" 2>&1 | tee /tmp/wasm_node_test.log
```

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

## Fiction

Yes: if you start the watcher and then record/save in Bitwig, files are auto-published for `fiction`.

### 1) Story IDs (required)

Voice lines are keyed by `[id:...]` in `stories/la_voiture.md`.

Examples:

```text
#∆V [id:handle_clicks_open] La poignée est froide...
:: [id:choice_open_handle] Ouvrir la poignée.
```

Rule:
- `:dialogue` and `:choice` lines trigger VO.
- Narration triggers VO only when it has explicit `[id:...]`.
- Plain continuation narration (no `[id:...]`) does not start a new clip.

### 2) Start the game

```bash
export JANK_SRC=/Users/pfeodrippe/dev/jank/compiler+runtime
export PATH="$JANK_SRC/build:$PATH"
make fiction
```

### 3) Start the auto-publisher watcher

Point `--source-dir` to your Bitwig output folder (recommended: project `master-recordings`).

```bash
python3 /Users/pfeodrippe/dev/something/bin/voice_auto_publish.py \
  --source-dir "/ABS/PATH/TO/BITWIG_PROJECT/master-recordings" \
  --target-root "/Users/pfeodrippe/dev/something/resources/fiction/voice" \
  --locale fr \
  --story-file "/Users/pfeodrippe/dev/something/stories/la_voiture.md" \
  --ffmpeg --sample-rate 48000 --channels 1 --codec pcm_s24le
```

### 4) File naming in Bitwig output

Source filename stem should be the line id.

Example:

```text
handle_clicks_open.wav
```

This publishes to:

```text
resources/fiction/voice/fr/handle_clicks_open.wav
```

### 5) Quick test loop

1. Keep watcher running.
2. Record/save in Bitwig.
3. Watcher logs `publish_ok`.
4. Trigger that line in `fiction`; newest audio plays.
