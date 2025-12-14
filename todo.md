- [x] standalone sdf script
- [ ] remove more stuff from sdf_engine.hpp
  - [ ] dispatch-compute!
    - [ ] (vk/vkCmdBindPipeline vk-cmd vk/VK_PIPELINE_BIND_POINT_COMPUTE (sdfx/get_compute_pipeline))
  - [ ] macro to set multiple fields using map
- [ ] sdf
  - [ ] sdf to obj in runtime
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
- [ ] how can we eliminate jit at all when not needed?


# Jank

- [x] void as nil
  - 97fed5973
