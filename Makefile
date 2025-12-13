# Makefile for something project
# Manages builds, cache, and common operations

.PHONY: clean clean-cache sdf integrated imgui jolt test tests help

# Default target
help:
	@echo "Available targets:"
	@echo "  make sdf         - Run SDF Vulkan viewer (dev mode)"
	@echo "  make integrated  - Run integrated demo (Raylib+ImGui+Jolt+Flecs)"
	@echo "  make imgui       - Run ImGui demo"
	@echo "  make jolt        - Run Jolt physics demo"
	@echo "  make test        - Run tests"
	@echo ""
	@echo "  make clean       - Clean all build artifacts and cache"
	@echo "  make clean-cache - Clean only jank module cache (target/)"
	@echo ""
	@echo "Standalone builds:"
	@echo "  make sdf-standalone  - Build standalone SDF viewer app"

# Clean jank module cache (fixes stale compilation issues)
clean-cache:
	@echo "Cleaning jank module cache..."
	rm -rf target/
	@echo "Done."

# Clean all build artifacts
clean: clean-cache
	@echo "Cleaning build artifacts..."
	rm -rf vulkan/imgui/*.o vulkan/stb_impl.o vulkan/libsdf_deps.dylib
	rm -rf vendor/imgui/build/*.o
	rm -rf vendor/jolt_wrapper.o
	rm -rf *.app
	@echo "Done."

# Run targets (clean cache first to avoid stale module issues)
sdf: clean-cache
	./bin/run_sdf.sh

integrated: clean-cache
	./bin/run_integrated.sh

imgui:
	./bin/run_imgui.sh

jolt:
	./bin/run_jolt.sh

test tests:
	./bin/run_tests.sh

# Standalone build
sdf-standalone: clean-cache
	./bin/run_sdf.sh --standalone -o SDFViewer
