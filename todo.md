- [x] standalone sdf script
- [x] remove more stuff from sdf_engine.hpp
  - [x] dispatch-compute!
    - [x] (vk/vkCmdBindPipeline vk-cmd vk/VK_PIPELINE_BIND_POINT_COMPUTE (sdfx/get_compute_pipeline))
  - [x] copy-image-to-buffer!
  - [-] write-png-downsampled
    - [-] gc-issue
      - unable to fix it, see ai/20251214-jank-gc-large-loops.md
  - [x] macro to set multiple fields using map
- [x] sdf
  - [x] sdf to obj in runtime
  - [x] with color
  - [x] generate it faster
  - [x] make hand-exploding animation
- [x] `make sdf` initialization really slow!
  - just by removing cache-clean
- [x] perf
  - [x]  unbox/box that's the issue?
    - unbox
- [x] audio
  - [x] most simple audio lib to use
    - [x] miniaudio ?
      - https://github.com/mackron/miniaudio
      - https://miniaud.io/docs/manual
- [x] draw-debug-ui! form is really slow to compile/eval
- [-] when we evaluate `should-close?` via the nREPL .app (after evaluating`(cpp/raw "#include \"vulkan/sdf_engine.hpp\"")`), we have the app closing
  - better to just move things to jank as much as possible
- [x] ios
  - [x] running
  - [x] touch event
  - [x] drag event
  - [x] make sdf-clean
  - [x] device
  - [x] show mesh
  - [x] improve script
- [x] jit (at least for dev)
  - [x] compile
  - [-] nREPL ios
    - takes too much memory
  - [x] ios-eval
    - cljs-like
  - [x] compiler server
    - [x] simulator
    - [x] make it faster
    - [x] device
  - [x] emacs nrepl
    - [x] build in ci
- [ ] ios native
- [ ] add jolt to sdf project
- [ ] ios
  - [x] new namespaces
  - [ ] fix namespace info and eldoc nREPL ops
  - [ ] Fix eid defn || 1. Unhandled internal/codegen-failure
   Remote compilation failed: analyze/invalid-cpp-call: No matching
   call to 'ecs_lookup' function. With argument 0 having type
   'jank::runtime::object *'. With argument 1 having type
   'jank::runtime::object *'.
   {:jank/error-kind "internal/codegen-failure" :jank/error-message "Remote compilation failed: analyze/invalid-cpp-call: No matching call to 'ecs_lookup' function. With argument 0 having type 'jank::runtime::object *'. With argument 1 having type 'jank::runtime::object *'." :clojure.error/phase :compile}
                       nil:   -1  internal/codegen-failure/internal/codegen-failure
  - [ ]

- [ ] create sdf core patterns
- [ ] wasm
- [ ] how can we eliminate jit at all when not needed?
- [ ] print atom
- [ ] linux
  - [ ] tests
  - [ ] standalone
- [ ] ios release mode

## Jank

- [x] void as nil
  - 97fed5973
- [x] unbox perf
- [ ] aot vs jit for new-frame!
- [ ] aot vs jit unbox at draw-debug-ui!

## Others

- [ ] jank-focused llm
- [ ] check flecs
- [ ] ipad simple text-focused app
