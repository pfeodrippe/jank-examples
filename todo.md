- [x] standalone sdf script
- [x] remove more stuff from sdf_engine.hpp
  - [x] dispatch-compute!
    - [x] (vk/vkCmdBindPipeline vk-cmd vk/VK_PIPELINE_BIND_POINT_COMPUTE (sdfx/get_compute_pipeline))
  - [x] copy-image-to-buffer!
  - [-] write-png-downsampled
    - [-] gc-issue
      - unable to fix it, see ai/20251214-jank-gc-large-loops.md
  - [x] macro to set multiple fields using map
- [ ] sdf
  - [x] sdf to obj in runtime
  - [ ] with texture?
  - [ ] generate it faster
  - [ ] make hand-exploding animation
- [ ] linux
  - [ ] tests
  - [ ] standalone
- [ ] audio
  - [ ] most simple audio lib to use
    - [ ] miniaudio ?
      - https://github.com/mackron/miniaudio
      - https://miniaud.io/docs/manual
- [-] when we evaluate `should-close?` via the nREPL .app (after evaluating`(cpp/raw "#include \"vulkan/sdf_engine.hpp\"")`), we have the app closing
  - better to just move things to jank as much as possible
- [ ] ios sample
  - townscaper-like
- [ ] wasm
- [ ] how can we eliminate jit at all when not needed?

## Jank

- [x] void as nil
  - 97fed5973

## Others

- [ ] jank-focused llm
