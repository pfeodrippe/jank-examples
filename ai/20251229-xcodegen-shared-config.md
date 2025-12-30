# XcodeGen Shared Configuration Refactoring

## Summary
Refactored iOS project YAML files to use XcodeGen's `include` and `targetTemplates` features to reduce duplication.

## Files Created
- `SdfViewerMobile/config-common.yml` - Shared configuration with templates and settings groups

## What Can Be Shared (Works)

### 1. Target Templates (`targetTemplates`)
Templates merge sources, dependencies, and preBuildScripts. Used for:
- **CommonSources** - main.mm, sdf_viewer_ios.mm, ImGui, STB, Flecs, sdf_engine_impl.cpp, resources, dependencies (MoltenVK, SDL3), shader compilation script
- **JITResources** - JIT-specific resources (jank-resources/*)

### 2. Setting Groups (`settingGroups`)
Groups work well for settings that don't need to be merged with target-specific values:
- **CommonHeaders** - HEADER_SEARCH_PATHS (jank headers, vendor headers, framework headers)
- **CommonCodeSign** - CODE_SIGN_IDENTITY, CODE_SIGNING_REQUIRED, CODE_SIGNING_ALLOWED

### 3. Global Options and Settings
- `options` (bundleIdPrefix, deploymentTarget, xcodeVersion)
- `settings.base` (DEVELOPMENT_TEAM, CODE_SIGN_STYLE, ENABLE_BITCODE, CLANG_CXX_*)

## What Cannot Be Shared (Limitation!)

### OTHER_LDFLAGS (Linker Flags)
**XcodeGen settingGroups do NOT merge arrays** - they **replace** them. So if you use:
```yaml
settings:
  groups: [CommonFrameworks]  # Has -framework Metal, etc.
  base:
    OTHER_LDFLAGS:
      - "-ljank"  # This REPLACES CommonFrameworks, not merges!
```

**Solution**: Include all frameworks directly in each target's OTHER_LDFLAGS.

## Line Count Reduction

| File | Before | After | Reduction |
|------|--------|-------|-----------|
| project.yml | 179 | ~80 | 55% |
| project-jit-sim.yml | 207 | ~97 | 53% |
| project-jit-device.yml | 208 | ~97 | 53% |
| project-jit-only-sim.yml | 202 | ~97 | 52% |
| project-jit-only-device.yml | 198 | ~100 | 50% |
| project-jit.yml | 193 | ~97 | 50% |

## Usage Pattern

```yaml
# In project-*.yml files:
include:
  - config-common.yml

targets:
  MyTarget:
    templates: [CommonSources]  # Or [CommonSources, JITResources] for JIT
    settings:
      groups: [CommonHeaders, CommonCodeSign]
      base:
        # Target-specific settings
        OTHER_LDFLAGS:
          # ALL frameworks must be listed here (arrays don't merge!)
          - "-framework Metal"
          # ... all frameworks ...
          # Target-specific libraries
          - "-ljank"
```

## Benefits
1. **Single source of truth for sources** - Add new source files once in config-common.yml
2. **Consistent headers** - Header search paths defined once
3. **Consistent code signing** - Code signing settings defined once
4. **Templates for common patterns** - CommonSources + JITResources

## Caveats
1. **OTHER_LDFLAGS cannot be shared** - Must duplicate framework list in each file
2. **HEADER_SEARCH_PATHS in groups can't be extended** - JIT-specific LLVM headers must be in target settings

## IMPORTANT: JIT Projects Must NOT Use CommonHeaders Group

JIT projects (`project-jit-*.yml`) need additional LLVM/CppInterop headers. Because XcodeGen `settingGroups` **replace** arrays (not merge), using `CommonHeaders` group and then specifying additional `HEADER_SEARCH_PATHS` will **replace** the common headers, not extend them.

**Wrong approach** (causes build failure - 'imgui.h' not found):
```yaml
settings:
  groups: [CommonHeaders, CommonCodeSign]  # CommonHeaders defines HEADER_SEARCH_PATHS
  base:
    HEADER_SEARCH_PATHS:  # This REPLACES CommonHeaders paths!
      - ${HOME}/dev/ios-llvm-build/ios-llvm-simulator/include
```

**Correct approach**:
```yaml
settings:
  groups: [CommonCodeSign]  # NO CommonHeaders!
  base:
    # All headers must be listed explicitly
    HEADER_SEARCH_PATHS:
      - $(PROJECT_DIR)/../vendor/imgui
      - $(PROJECT_DIR)/../vendor/imgui/backends
      - $(PROJECT_DIR)/../vendor/flecs/distr
      - $(PROJECT_DIR)/../vendor
      - $(PROJECT_DIR)/../vulkan
      - $(PROJECT_DIR)/Frameworks/include
      - $(PROJECT_DIR)/Frameworks/include/SDL3
      - $(PROJECT_DIR)/Frameworks/include/vulkan
      - $(PROJECT_DIR)/Frameworks/include/MoltenVK
      - ${JANK_SRC}/include/cpp
      - ${JANK_SRC}/src/cpp
      - ${JANK_SRC}/third-party/immer
      - ${JANK_SRC}/third-party/bdwgc/include
      - ${JANK_SRC}/third-party/bpptree/include
      - ${JANK_SRC}/third-party/boost-preprocessor/include
      - ${JANK_SRC}/third-party/boost-multiprecision/include
      - ${JANK_SRC}/third-party/folly
      - ${JANK_SRC}/third-party/stduuid/include
      # JIT-specific headers
      - ${HOME}/dev/ios-llvm-build/ios-llvm-simulator/include
      - ${JANK_SRC}/third-party/cppinterop/include
    FRAMEWORK_SEARCH_PATHS:
      - $(PROJECT_DIR)/Frameworks
    LD_RUNPATH_SEARCH_PATHS:
      - "@executable_path/Frameworks"
```

**Files affected**: `project-jit-sim.yml`, `project-jit-device.yml`, `project-jit-only-sim.yml`, `project-jit-only-device.yml`, `project-jit.yml`

## Commands
- Generate project: `xcodegen generate --spec project.yml`
- Test build: `make sdf-ios-sim-build`

## Resources
- [XcodeGen ProjectSpec](https://github.com/yonaskolb/XcodeGen/blob/master/Docs/ProjectSpec.md)
- [XcodeGen Include](https://github.com/yonaskolb/XcodeGen/blob/master/Docs/ProjectSpec.md#include)
