# Makefile for something project
# Manages builds, cache, and common operations

.PHONY: clean clean-cache sdf integrated imgui jolt test tests help \
        build-jolt build-imgui build-flecs build-raylib build-deps

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
	@echo "  make build-deps  - Build all dependencies (Jolt, ImGui, Flecs)"
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
	rm -rf vulkan/imgui/*.o vulkan/stb_impl.o vulkan/tinygltf_impl.o vulkan/libsdf_deps.dylib
	rm -rf vendor/imgui/build/*.o
	rm -rf vendor/jolt_wrapper.o
	rm -rf vendor/JoltPhysics/build
	rm -rf vendor/JoltPhysics/distr/objs
	rm -rf *.app
	@echo "Done."

# Build dependencies
build-jolt:
	@if [ ! -f "vendor/jolt_wrapper.o" ]; then \
		echo "Building JoltPhysics..."; \
		bash ./build_jolt.sh; \
	fi

build-imgui:
	@if [ ! -f "vendor/imgui/build/imgui.o" ]; then \
		echo "Building ImGui..."; \
		bash ./build_imgui.sh; \
	fi

build-flecs:
	@if [ ! -f "vendor/flecs/distr/flecs.o" ]; then \
		echo "Building Flecs..."; \
		cd vendor/flecs/distr && clang -c -fPIC -o flecs.o flecs.c; \
	fi

build-raylib:
	@if [ ! -f "vendor/raylib/distr/libraylib_jank.a" ]; then \
		echo "Building Raylib..."; \
		bash ./build_raylib.sh; \
	fi

build-deps: build-jolt build-imgui build-flecs build-raylib
	@echo "All dependencies built."

# Run targets (clean cache first to avoid stale module issues)
sdf: clean-cache
	./bin/run_sdf.sh

integrated: clean-cache
	./bin/run_integrated.sh

imgui:
	./bin/run_imgui.sh

jolt:
	./bin/run_jolt.sh

test tests: build-flecs
	./bin/run_tests.sh

# Standalone build
sdf-standalone: clean-cache
	./bin/run_sdf.sh --standalone -o SDFViewer
