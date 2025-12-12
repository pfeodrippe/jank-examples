# Suggested Commands

## Using Makefile (Recommended)
```bash
make help             # Show all available targets
make sdf              # Run SDF Vulkan viewer (auto-cleans cache)
make integrated       # Run Raylib + ImGui + Jolt + Flecs demo
make imgui            # Run ImGui demo
make jolt             # Run Jolt physics demo
make tests            # Run tests
make sdf-standalone   # Build portable .app bundle
make clean-cache      # Clean jank module cache (fixes stale compilation)
make clean            # Clean all build artifacts
```

## Direct Scripts
```bash
./bin/run_sdf.sh                          # SDF Vulkan viewer
./bin/run_integrated.sh                   # Raylib + ImGui + Jolt + Flecs demo
./bin/run_tests.sh                        # Run tests
./bin/run_sdf.sh --standalone -o SDFViewer  # Create portable .app bundle
./bin/clean_cache.sh                      # Clean jank module cache
```

## WASM Build
```bash
./bin/run_integrated_wasm.sh
```

## Build Dependencies
```bash
./build_imgui.sh
./build_jolt.sh
```

## Troubleshooting
If you get "Failed to find symbol: 'jank_load_*'" errors:
```bash
make clean-cache  # or: rm -rf target/
```
