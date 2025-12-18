# Makefile for something project
# Manages builds, cache, and common operations

# Platform detection
UNAME_S := $(shell uname -s)

# Compiler settings
CC = clang
CXX = clang++

# jank paths (for vybe_flecs_jank.o which needs jank headers)
JANK_SRC ?= /Users/pfeodrippe/dev/jank/compiler+runtime
JANK_CXX = $(JANK_SRC)/build/llvm-install/usr/local/bin/clang++

# Platform-specific settings
ifeq ($(UNAME_S),Darwin)
    SHARED_LIB_EXT = dylib
    HOMEBREW_PREFIX = /opt/homebrew
    SDL3_CFLAGS = -I$(HOMEBREW_PREFIX)/include -I$(HOMEBREW_PREFIX)/include/SDL3
    VULKAN_CFLAGS = -I$(HOMEBREW_PREFIX)/include
    VULKAN_LIBS = -L$(HOMEBREW_PREFIX)/lib -lvulkan -lSDL3 -lshaderc_shared
    FRAMEWORKS = -framework Cocoa -framework IOKit -framework IOSurface -framework Metal -framework QuartzCore
    SHARED_FLAGS = -dynamiclib -Wl,-undefined,dynamic_lookup
    # Shader compiler (prefer glslangValidator on macOS)
    GLSLC := $(shell command -v glslangValidator 2>/dev/null || command -v glslc 2>/dev/null)
    ifeq ($(findstring glslangValidator,$(GLSLC)),glslangValidator)
        GLSLC_FLAGS = -V
    else
        GLSLC_FLAGS =
    endif
else
    SHARED_LIB_EXT = so
    SDL3_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null || echo "-I/usr/local/include -I/usr/local/include/SDL3")
    VULKAN_CFLAGS = $(shell pkg-config --cflags vulkan 2>/dev/null || echo "")
    VULKAN_LIBS = -L/usr/lib -L/usr/lib/x86_64-linux-gnu -L/usr/lib/aarch64-linux-gnu -L/usr/local/lib -lvulkan -lSDL3 -lshaderc
    FRAMEWORKS =
    SHARED_FLAGS = -shared -fPIC -Wl,--allow-shlib-undefined
    GLSLC := $(shell command -v glslangValidator 2>/dev/null || command -v glslc 2>/dev/null)
    ifeq ($(findstring glslangValidator,$(GLSLC)),glslangValidator)
        GLSLC_FLAGS = -V
    else
        GLSLC_FLAGS =
    endif
endif

# Common flags
CFLAGS = -fPIC -O2
CXXFLAGS = -fPIC -O2 -std=c++17

.PHONY: clean clean-cache sdf integrated imgui jolt test tests help \
        build-jolt build-imgui build-flecs build-raylib build-deps \
        build-sdf-deps build-shaders build-imgui-vulkan

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
	@echo "  make build-deps     - Build all dependencies (Jolt, ImGui, Flecs)"
	@echo "  make build-sdf-deps - Build SDF dependencies (imgui, shaders, obj files)"
	@echo ""
	@echo "Standalone builds:"
	@echo "  make sdf-standalone  - Build standalone SDF viewer app"

# ============================================================================
# Object file targets (with proper dependencies)
# ============================================================================

# ImGui objects for Vulkan/SDL3 backend
IMGUI_DIR = vendor/imgui
IMGUI_VULKAN_OBJS = vulkan/imgui/imgui.o vulkan/imgui/imgui_draw.o vulkan/imgui/imgui_widgets.o \
                    vulkan/imgui/imgui_tables.o vulkan/imgui/imgui_impl_sdl3.o vulkan/imgui/imgui_impl_vulkan.o

IMGUI_CXXFLAGS = $(CXXFLAGS) -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(SDL3_CFLAGS) $(VULKAN_CFLAGS)

vulkan/imgui/imgui.o: $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

vulkan/imgui/imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

vulkan/imgui/imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

vulkan/imgui/imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui.h $(IMGUI_DIR)/imgui_internal.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

vulkan/imgui/imgui_impl_sdl3.o: $(IMGUI_DIR)/backends/imgui_impl_sdl3.cpp $(IMGUI_DIR)/backends/imgui_impl_sdl3.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

vulkan/imgui/imgui_impl_vulkan.o: $(IMGUI_DIR)/backends/imgui_impl_vulkan.cpp $(IMGUI_DIR)/backends/imgui_impl_vulkan.h
	@mkdir -p vulkan/imgui
	$(CXX) $(IMGUI_CXXFLAGS) -c $< -o $@

build-imgui-vulkan: $(IMGUI_VULKAN_OBJS)
	@echo "ImGui Vulkan/SDL3 objects built."

# STB implementation
vulkan/stb_impl.o: vulkan/stb_impl.c vulkan/stb_image_write.h
	$(CC) $(CFLAGS) -Ivulkan -c $< -o $@

# TinyGLTF implementation (for GLB export)
vulkan/tinygltf_impl.o: vulkan/tinygltf_impl.cpp vulkan/marching_cubes.hpp
	$(CXX) $(CXXFLAGS) -I. -Ivendor -c $< -o $@

# Flecs ECS library
vendor/flecs/distr/flecs.o: vendor/flecs/distr/flecs.c vendor/flecs/distr/flecs.h
	$(CC) $(CFLAGS) -c $< -o $@

# Vybe Flecs jank helper (requires jank's clang for header compatibility)
VYBE_FLECS_JANK_INCLUDES = \
	-DIMMER_HAS_LIBGC=1 \
	-I$(JANK_SRC)/include/cpp \
	-I$(JANK_SRC)/third-party \
	-I$(JANK_SRC)/third-party/bdwgc/include \
	-I$(JANK_SRC)/third-party/immer \
	-I$(JANK_SRC)/third-party/bpptree/include \
	-I$(JANK_SRC)/third-party/folly \
	-I$(JANK_SRC)/third-party/boost-multiprecision/include \
	-I$(JANK_SRC)/third-party/boost-preprocessor/include \
	-I$(JANK_SRC)/build/llvm-install/usr/local/include \
	-Ivendor -Ivendor/flecs/distr

vendor/vybe/vybe_flecs_jank.o: vendor/vybe/vybe_flecs_jank.cpp vendor/vybe/vybe_flecs_jank.h vendor/flecs/distr/flecs.h
	$(JANK_CXX) -std=c++20 -fPIC $(VYBE_FLECS_JANK_INCLUDES) -c $< -o $@

# ============================================================================
# Shader compilation
# ============================================================================

# Basic rendering shaders
SHADERS_BASIC = vulkan_kim/blit.vert.spv vulkan_kim/blit.frag.spv \
                vulkan_kim/mesh.vert.spv vulkan_kim/mesh.frag.spv

# Compute shaders for SDF and mesh generation
SHADERS_COMPUTE = vulkan_kim/sdf_sampler.spv vulkan_kim/sdf_scene.spv \
                  vulkan_kim/dc_mark_active.spv vulkan_kim/dc_vertices.spv \
                  vulkan_kim/dc_quads.spv vulkan_kim/dc_cubes.spv

# All shaders
SHADERS_SPV = $(SHADERS_BASIC) $(SHADERS_COMPUTE)

# Compile vertex/fragment shaders (blit.vert -> blit.vert.spv)
vulkan_kim/%.vert.spv: vulkan_kim/%.vert
ifdef GLSLC
	$(GLSLC) $(GLSLC_FLAGS) $< -o $@
else
	@echo "Warning: No GLSL compiler found, skipping $@"
endif

vulkan_kim/%.frag.spv: vulkan_kim/%.frag
ifdef GLSLC
	$(GLSLC) $(GLSLC_FLAGS) $< -o $@
else
	@echo "Warning: No GLSL compiler found, skipping $@"
endif

# Compile compute shaders (dc_mark_active.comp -> dc_mark_active.spv)
vulkan_kim/%.spv: vulkan_kim/%.comp
ifdef GLSLC
	$(GLSLC) $(GLSLC_FLAGS) $< -o $@
else
	@echo "Warning: No GLSL compiler found, skipping $@"
endif

build-shaders: $(SHADERS_SPV)
	@echo "Shaders compiled."

# ============================================================================
# SDF dependencies shared library
# ============================================================================

# Object files needed for JIT mode
SDF_JIT_OBJS = $(IMGUI_VULKAN_OBJS) vulkan/stb_impl.o vendor/flecs/distr/flecs.o vendor/vybe/vybe_flecs_jank.o

# Additional objects only needed for standalone (AOT) builds
SDF_STANDALONE_OBJS = vulkan/tinygltf_impl.o

# All SDF objects
SDF_ALL_OBJS = $(SDF_JIT_OBJS) $(SDF_STANDALONE_OBJS)

# Shared library for SDF viewer (includes all objects for standalone builds)
vulkan/libsdf_deps.$(SHARED_LIB_EXT): $(SDF_ALL_OBJS)
	$(CXX) $(SHARED_FLAGS) -o $@ $(SDF_ALL_OBJS) $(FRAMEWORKS) $(VULKAN_LIBS)

# Build all SDF dependencies
build-sdf-deps: $(SDF_JIT_OBJS) $(SHADERS_SPV)
	@echo "SDF dependencies built (JIT mode)."

build-sdf-deps-standalone: $(SDF_ALL_OBJS) $(SHADERS_SPV) vulkan/libsdf_deps.$(SHARED_LIB_EXT)
	@echo "SDF dependencies built (standalone mode)."

# ============================================================================
# Clean targets
# ============================================================================

# Clean jank module cache (fixes stale compilation issues)
clean-cache:
	@echo "Cleaning jank module cache..."
	rm -rf target/
	rm -f .jank-headers-stamp
	@echo "Done."

# Clean all build artifacts
clean: clean-cache
	@echo "Cleaning build artifacts..."
	rm -rf vulkan/imgui/*.o vulkan/stb_impl.o vulkan/tinygltf_impl.o
	rm -rf vulkan/libsdf_deps.dylib vulkan/libsdf_deps.so
	rm -rf vendor/imgui/build/*.o
	rm -rf vendor/vybe/vybe_flecs_jank.o
	rm -rf vendor/flecs/distr/flecs.o
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

# ============================================================================
# Jank header dependency tracking
# ============================================================================
# Jank doesn't track C++ header dependencies for cache invalidation.
# This dynamically finds which .jank files include changed headers and invalidates only those.

# Headers that jank files may include - add new headers here
JANK_NATIVE_HEADERS = vulkan/sdf_engine.hpp vulkan/marching_cubes.hpp

# Stamp file tracks when headers were last checked
# $? contains only the headers that changed (newer than stamp)
.jank-headers-stamp: $(JANK_NATIVE_HEADERS)
	@for header in $?; do \
		echo "$$header changed, finding affected modules..."; \
		for jank_file in $$(find src -name "*.jank" -exec grep -l "$$header" {} \; 2>/dev/null); do \
			mod_path=$$(echo "$$jank_file" | sed 's|^src/||; s|\.jank$$||'); \
			if ls target/*/$$mod_path.o 1>/dev/null 2>&1; then \
				echo "  Invalidating $$mod_path"; \
				rm -f target/*/$$mod_path.o; \
			fi; \
		done; \
	done
	@touch $@

# Run targets
sdf: build-sdf-deps .jank-headers-stamp
	./bin/run_sdf.sh

sdf-clean: clean-cache build-sdf-deps
	./bin/run_sdf.sh

sdf-standalone: clean-cache build-sdf-deps-standalone
	./bin/run_sdf.sh --standalone -o SDFViewer

integrated: clean-cache
	./bin/run_integrated.sh

imgui:
	./bin/run_imgui.sh

jolt:
	./bin/run_jolt.sh

test tests: build-flecs
	./bin/run_tests.sh
