# Makefile for something project
# Manages builds, cache, and common operations

# Platform detection
UNAME_S := $(shell uname -s)

# Compiler settings
CC = clang
CXX = clang++

# jank paths (for vybe_flecs_jank.o which needs jank headers)
JANK_SRC ?= /Users/pfeodrippe/dev/jank/compiler+runtime
export JANK_SRC  # Export so shell scripts can access it
JANK_CXX = $(JANK_SRC)/build/llvm-install/usr/local/bin/clang++

# macOS SDK path (needed for system headers when using jank's clang)
ifeq ($(UNAME_S),Darwin)
    SDKROOT ?= /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
endif

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
        # -V = Vulkan SPIR-V, -I = include path for #include directives
        GLSLC_FLAGS = -V -I vulkan_kim
    else
        GLSLC_FLAGS = -I vulkan_kim
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
        # -V = Vulkan SPIR-V, -I = include path for #include directives
        GLSLC_FLAGS = -V -I vulkan_kim
    else
        GLSLC_FLAGS = -I vulkan_kim
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
	@echo "═══════════════════════════════════════════════════════════════════════"
	@echo "                         AVAILABLE TARGETS"
	@echo "═══════════════════════════════════════════════════════════════════════"
	@echo ""
	@echo "── Desktop ─────────────────────────────────────────────────────────────"
	@echo "  make sdf              - Run SDF Vulkan viewer (dev mode)"
	@echo "  make sdf-clean        - Run SDF viewer with fresh cache"
	@echo "  make sdf-standalone   - Build standalone SDF viewer app"
	@echo "  make integrated       - Run integrated demo (Raylib+ImGui+Jolt+Flecs)"
	@echo "  make imgui            - Run ImGui demo"
	@echo "  make jolt             - Run Jolt physics demo"
	@echo "  make test             - Run tests"
	@echo ""
	@echo "── Build & Clean ───────────────────────────────────────────────────────"
	@echo "  make build-deps       - Build all dependencies (Jolt, ImGui, Flecs)"
	@echo "  make build-sdf-deps   - Build SDF dependencies (imgui, shaders, obj)"
	@echo "  make clean            - Clean all build artifacts and cache"
	@echo "  make clean-cache      - Clean only jank module cache (target/)"
	@echo ""
	@echo "── iOS AOT (Full App Bundle) ────────────────────────────────────────────"
	@echo "  make ios-aot-sim-run        - Build and run AOT app in iPad simulator"
	@echo "  make ios-aot-device-run     - Build and run AOT app on connected device"
	@echo "  make ios-setup              - Download/build iOS dependencies"
	@echo "  make ios-project            - Generate Xcode project"
	@echo "  make ios-build              - Build iOS app"
	@echo "  make ios-clean              - Clean iOS build artifacts"
	@echo ""
	@echo "── iOS JIT (Remote Compile Server - RECOMMENDED FOR DEV) ─────────────────"
	@echo "  Core libs bundled, app namespaces loaded via remote compile server."
	@echo "  No redeploy needed for code changes - just eval via nREPL!"
	@echo "  Compile server auto-starts and restarts with each run."
	@echo ""
	@echo "  One-time setup:"
	@echo "    make ios-jit-llvm-sim     - Build LLVM for Simulator (~2hrs)"
	@echo "    make ios-jit-llvm-device  - Build LLVM for Device (~2hrs)"
	@echo ""
	@echo "  Simulator (port 5570):"
	@echo "    make ios-jit-sim-run        - Build and run (auto-starts server)"
	@echo "    make ios-compile-server-sim - Start compile server standalone"
	@echo ""
	@echo "  Device (port 5571):"
	@echo "    make ios-jit-device-run        - Build and run (auto-starts server+iproxy)"
	@echo "    make ios-compile-server-device - Start compile server standalone"
	@echo "    make ios-device-nrepl-proxy    - Start iproxy (Mac:5559 -> Device:5558)"
	@echo ""
	@echo "  Typical usage: make ios-jit-device-run"
	@echo "  Then connect nREPL to localhost:5559"
	@echo "═══════════════════════════════════════════════════════════════════════"

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
# Key jank headers that affect ABI - rebuild vybe_flecs_jank.o when these change
JANK_ABI_HEADERS = $(JANK_SRC)/include/cpp/jank/type.hpp \
                   $(JANK_SRC)/include/cpp/jank/runtime/core/jank_heap.hpp

VYBE_FLECS_JANK_INCLUDES = \
	-DIMMER_HAS_LIBGC=1 \
	-I$(JANK_SRC)/include/cpp \
	-I$(JANK_SRC)/third-party \
	-I$(JANK_SRC)/third-party/bdwgc/include \
	-I$(JANK_SRC)/third-party/immer \
	-I$(JANK_SRC)/third-party/folly \
	-I$(JANK_SRC)/third-party/boost-multiprecision/include \
	-I$(JANK_SRC)/third-party/boost-preprocessor/include \
	-I$(JANK_SRC)/third-party/stduuid/include \
	-I$(JANK_SRC)/third-party/cppinterop/include \
	-I$(JANK_SRC)/build/llvm-install/usr/local/include \
	-Ivendor -Ivendor/flecs/distr

# macOS needs --sysroot for system headers when using jank's clang
ifeq ($(UNAME_S),Darwin)
    VYBE_FLECS_JANK_SYSROOT = --sysroot=$(SDKROOT)
else
    VYBE_FLECS_JANK_SYSROOT =
endif

vendor/vybe/vybe_flecs_jank.o: vendor/vybe/vybe_flecs_jank.cpp vendor/vybe/vybe_flecs_jank.h vendor/flecs/distr/flecs.h $(JANK_ABI_HEADERS)
	$(JANK_CXX) -std=c++20 -fPIC $(VYBE_FLECS_JANK_SYSROOT) $(VYBE_FLECS_JANK_INCLUDES) -c $< -o $@

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

# Miniaudio object (impl file lives outside submodule so it's tracked by git)
vendor/vybe/miniaudio.o: vendor/vybe/miniaudio_impl.c vendor/miniaudio/miniaudio.h
	$(CC) $(CFLAGS) -c $< -o $@

# Object files needed for JIT mode
SDF_JIT_OBJS = $(IMGUI_VULKAN_OBJS) vulkan/stb_impl.o vendor/flecs/distr/flecs.o vendor/vybe/vybe_flecs_jank.o vendor/vybe/miniaudio.o

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
	rm -rf vendor/vybe/miniaudio.o
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

# Run SDF viewer with iOS compile server enabled (for remote iOS JIT)
# NOTE: This is the SLOW integrated server - use ios-compile-server instead
sdf-ios-server: clean-cache build-sdf-deps
	./bin/run_sdf.sh --ios-compile-server 5570 --ios-resource-dir $(PWD)/SdfViewerMobile/jank-resources

# List vybe modules transitively used by vybe.sdf.ios
.PHONY: check-jank-modules
check-jank-modules:
	@$(PWD)/bin/list-vybe-deps.sh vybe.sdf.ios

# Evaluate all vybe modules via nREPL (requires iOS app running on port 5558)
.PHONY: check-jank-modules-nrepl
check-jank-modules-nrepl:
	@$(PWD)/bin/list-vybe-deps.sh vybe.sdf.ios 5558

# Compile server for iOS Simulator JIT development (port 5570)
# Always restarts to pick up latest code changes
.PHONY: ios-compile-server-sim
ios-compile-server-sim:
	@echo "Starting compile server for simulator (port 5570)..."
	@-pkill -f "compile-server.*--port 5570" 2>/dev/null || true
	@sleep 0.2
	@cd $(JANK_SRC) && ./build/compile-server --target sim --port 5570 \
		--module-path $(PWD)/SdfViewerMobile/jank-resources/src/jank:$(JANK_SRC)/../nrepl-server/src/jank \
		--jit-lib /opt/homebrew/lib/libvulkan.dylib \
		--jit-lib /opt/homebrew/lib/libSDL3.dylib \
		--jit-lib /opt/homebrew/lib/libshaderc_shared.dylib \
		--jit-lib $(PWD)/vulkan/libsdf_deps.dylib \
		-I $(PWD)/SdfViewerMobile/jank-resources/include \
		-I /opt/homebrew/include \
		-I /opt/homebrew/include/SDL3 \
		-I $(PWD) \
		-I $(PWD)/vendor \
		-I $(PWD)/vendor/imgui \
		-I $(PWD)/vendor/imgui/backends \
		-I $(PWD)/vendor/flecs/distr \
		-I $(PWD)/vendor/miniaudio & \
	echo "Waiting for compile server to be ready..."; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		sleep 0.5; \
		if lsof -i :5570 >/dev/null 2>&1; then \
			echo "Compile server ready!"; \
			break; \
		fi; \
	done

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

# ============================================================================
# iOS targets
# ============================================================================

.PHONY: ios-setup ios-project ios-build ios-clean ios-runtime ios-core-libs ios-aot-sim ios-aot-sim-run ios-aot-sim-build ios-aot-sim-clean ios-aot-device ios-aot-device-run ios-aot-device-clean \
        ios-jit-llvm-sim ios-jit-llvm-device ios-jit-sim ios-jit-device

# jank iOS build paths (device)
JANK_IOS_BUILD = $(JANK_SRC)/build-ios
JANK_IOS_LIBS = $(JANK_IOS_BUILD)/libjank.a $(JANK_IOS_BUILD)/libjankzip.a $(JANK_IOS_BUILD)/third-party/bdwgc/libgc.a
JANK_IOS_CORE_OBJS = $(JANK_IOS_BUILD)/clojure_core_generated.o \
                     $(JANK_IOS_BUILD)/clojure_set_generated.o \
                     $(JANK_IOS_BUILD)/clojure_string_generated.o \
                     $(JANK_IOS_BUILD)/clojure_walk_generated.o \
                     $(JANK_IOS_BUILD)/clojure_template_generated.o \
                     $(JANK_IOS_BUILD)/clojure_test_generated.o

# jank iOS Simulator build paths
JANK_IOS_SIM_BUILD = $(JANK_SRC)/build-ios-simulator
JANK_IOS_SIM_LIBS = $(JANK_IOS_SIM_BUILD)/libjank.a $(JANK_IOS_SIM_BUILD)/libjankzip.a $(JANK_IOS_SIM_BUILD)/third-party/bdwgc/libgc.a
JANK_IOS_SIM_CORE_OBJS = $(JANK_IOS_SIM_BUILD)/clojure_core_generated.o \
                         $(JANK_IOS_SIM_BUILD)/clojure_set_generated.o \
                         $(JANK_IOS_SIM_BUILD)/clojure_string_generated.o \
                         $(JANK_IOS_SIM_BUILD)/clojure_walk_generated.o \
                         $(JANK_IOS_SIM_BUILD)/clojure_template_generated.o \
                         $(JANK_IOS_SIM_BUILD)/clojure_test_generated.o

# iOS app generated files
IOS_GENERATED_CPP = SdfViewerMobile/generated/vybe_sdf_generated.cpp
IOS_GENERATED_OBJ = SdfViewerMobile/build/obj/vybe_sdf_generated.o
IOS_LOCAL_LIBS = SdfViewerMobile/build/libjank.a SdfViewerMobile/build/libjankzip.a SdfViewerMobile/build/libgc.a

# Setup iOS dependencies (downloads MoltenVK, builds SDL3)
ios-setup:
	@echo "Setting up iOS dependencies..."
	cd SdfViewerMobile && ./setup_ios_deps.sh

# Build jank runtime for iOS (device)
$(JANK_IOS_BUILD)/libjank.a:
	@echo "Building jank runtime for iOS device..."
	cd $(JANK_SRC) && ./bin/build-ios $(JANK_IOS_BUILD) Release device

# Build jank runtime for iOS Simulator
$(JANK_IOS_SIM_BUILD)/libjank.a:
	@echo "Building jank runtime for iOS Simulator..."
	cd $(JANK_SRC) && ./bin/build-ios $(JANK_IOS_SIM_BUILD) Release simulator

ios-runtime: $(JANK_IOS_BUILD)/libjank.a
	@echo "jank iOS device runtime built."

ios-sim-runtime: $(JANK_IOS_SIM_BUILD)/libjank.a
	@echo "jank iOS Simulator runtime built."

# Generate and compile clojure.core and other core libraries for iOS (device)
$(JANK_IOS_BUILD)/clojure_core_generated.o: $(JANK_SRC)/src/jank/clojure/core.jank $(JANK_SRC)/build/jank
	@echo "Generating clojure.core for iOS device..."
	cd $(JANK_SRC) && ./bin/ios-bundle --skip-build

# Generate and compile clojure.core and other core libraries for iOS Simulator
$(JANK_IOS_SIM_BUILD)/clojure_core_generated.o: $(JANK_SRC)/src/jank/clojure/core.jank $(JANK_SRC)/build/jank
	@echo "Generating clojure.core for iOS Simulator..."
	cd $(JANK_SRC) && ./bin/ios-bundle --skip-build --simulator --output-dir $(JANK_IOS_SIM_BUILD)

ios-core-libs: $(JANK_IOS_CORE_OBJS)
	@echo "jank core libraries built for iOS device."

ios-sim-core-libs: $(JANK_IOS_SIM_CORE_OBJS)
	@echo "jank core libraries built for iOS Simulator."

# Generate vybe.sdf C++ for iOS
$(IOS_GENERATED_CPP): src/vybe/sdf.jank build-sdf-deps $(JANK_SRC)/build/jank
	@echo "Generating vybe.sdf C++ for iOS..."
	./SdfViewerMobile/build_ios_jank_aot.sh

# Cross-compile vybe.sdf for iOS
$(IOS_GENERATED_OBJ): $(IOS_GENERATED_CPP)
	@echo "vybe.sdf already compiled by build script"

# Copy jank iOS device libraries to local build directory
$(IOS_LOCAL_LIBS): $(JANK_IOS_LIBS) $(JANK_IOS_CORE_OBJS)
	@echo "Copying jank iOS device libraries to local build..."
	@mkdir -p SdfViewerMobile/build/obj
	cp $(JANK_IOS_BUILD)/libjank.a SdfViewerMobile/build/
	cp $(JANK_IOS_BUILD)/libjankzip.a SdfViewerMobile/build/
	cp $(JANK_IOS_BUILD)/third-party/bdwgc/libgc.a SdfViewerMobile/build/
	cp $(JANK_IOS_CORE_OBJS) SdfViewerMobile/build/obj/

# Copy jank iOS Simulator libraries to local build directory
.PHONY: ios-sim-copy-libs
ios-sim-copy-libs: $(JANK_IOS_SIM_LIBS) $(JANK_IOS_SIM_CORE_OBJS)
	@echo "Copying jank iOS Simulator libraries to local build..."
	@mkdir -p SdfViewerMobile/build-iphonesimulator/obj
	cp $(JANK_IOS_SIM_BUILD)/libjank.a SdfViewerMobile/build-iphonesimulator/
	cp $(JANK_IOS_SIM_BUILD)/libjankzip.a SdfViewerMobile/build-iphonesimulator/
	cp $(JANK_IOS_SIM_BUILD)/third-party/bdwgc/libgc.a SdfViewerMobile/build-iphonesimulator/
	cp $(JANK_IOS_SIM_CORE_OBJS) SdfViewerMobile/build-iphonesimulator/obj/

# Copy jank iOS Device libraries to local build directory
.PHONY: ios-copy-libs
ios-copy-libs: $(JANK_IOS_LIBS) $(JANK_IOS_CORE_OBJS)
	@echo "Copying jank iOS Device libraries to local build..."
	@mkdir -p SdfViewerMobile/build-iphoneos/obj
	cp $(JANK_IOS_BUILD)/libjank.a SdfViewerMobile/build-iphoneos/
	cp $(JANK_IOS_BUILD)/libjankzip.a SdfViewerMobile/build-iphoneos/
	cp $(JANK_IOS_BUILD)/third-party/bdwgc/libgc.a SdfViewerMobile/build-iphoneos/
	cp $(JANK_IOS_CORE_OBJS) SdfViewerMobile/build-iphoneos/obj/

# Generate Xcode project using xcodegen
ios-project: build-shaders $(IOS_LOCAL_LIBS)
	@echo "Generating iOS Xcode project..."
	@if ! command -v xcodegen &> /dev/null; then \
		echo "Error: xcodegen not found. Install with: brew install xcodegen"; \
		exit 1; \
	fi
	cd SdfViewerMobile && ./generate-project.sh
	@echo "Xcode project generated: SdfViewerMobile/SdfViewerMobile.xcodeproj"

# Build iOS app (requires prior setup and project generation)
ios-build: ios-project
	@echo "Building iOS app..."
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile.xcodeproj \
		-scheme SdfViewerMobile \
		-configuration Release \
		-sdk iphoneos \
		-destination 'generic/platform=iOS' \
		build

# Clean iOS build artifacts
ios-clean:
	@echo "Cleaning iOS build artifacts..."
	rm -rf SdfViewerMobile/SdfViewerMobile.xcodeproj
	rm -rf SdfViewerMobile/build
	rm -rf SdfViewerMobile/generated
	rm -rf SdfViewerMobile/Frameworks
	rm -rf SdfViewerMobile/temp_build
	@echo "iOS artifacts cleaned"

# Clean iOS JIT build directories (use after jank header changes)
.PHONY: ios-jit-clean
ios-jit-clean:
	@echo "Cleaning iOS JIT build artifacts..."
	rm -rf SdfViewerMobile/build-iphonesimulator-jit
	rm -rf SdfViewerMobile/build-iphoneos-jit
	rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-*
	rm -rf $(JANK_DIR)/build-ios-sim-jit
	rm -rf $(JANK_DIR)/build-ios-device-jit
	rm -f SdfViewerMobile/jank-resources/incremental.pch
	rm -f SdfViewerMobile/jank-resources/incremental-device.pch
	@echo "iOS JIT artifacts cleaned"
	@echo "Run 'make ios-jit-sim-run' or 'make ios-jit-device-run' to rebuild"

# Build jank for iOS (AOT compilation) - generates C++ and cross-compiles
ios-jank: ios-runtime ios-core-libs $(IOS_GENERATED_OBJ)
	@echo "jank iOS build complete!"

# Legacy iOS jank build (old approach with weak stubs)
ios-jank-legacy:
	@echo "Building jank for iOS (legacy weak stubs)..."
	./SdfViewerMobile/build_ios_jank.sh

# Full iOS build with jank
ios: ios-setup ios-jank ios-project ios-build
	@echo "iOS app with jank built successfully!"

# Quick iOS jank C++ generation and cross-compilation (legacy target)
ios-legacy: clean-cache build-sdf-deps ios-jank ios-project
	@echo ""
	@echo "============================================"
	@echo "  iOS Build Complete!"
	@echo "============================================"
	@echo ""
	@echo "Libraries: SdfViewerMobile/build/"
	@echo "Objects:   SdfViewerMobile/build/obj/"
	@echo "Project:   SdfViewerMobile/SdfViewerMobile.xcodeproj"
	@echo ""
	@echo "To run in simulator: make ios-aot-sim-run"

# Build iOS AOT for simulator (incremental - reuses existing builds)
# The build script now handles: jank runtime build, module compilation, library bundling
.PHONY: ios-aot-sim
ios-aot-sim: build-sdf-deps-standalone build-shaders
	@echo "Building vybe.sdf for iOS Simulator (AOT)..."
	./SdfViewerMobile/build_ios_jank_aot.sh simulator
	@echo "Generating iOS Simulator Xcode project..."
	cd SdfViewerMobile && ./generate-project.sh project-aot-sim.yml
	@echo ""
	@echo "============================================"
	@echo "  iOS AOT Simulator Build Complete!"
	@echo "============================================"

# Build iOS AOT for simulator (clean rebuild)
.PHONY: ios-aot-sim-clean
ios-aot-sim-clean: clean-cache ios-aot-sim

# Build iOS AOT app for simulator (xcodebuild only, no launch)
.PHONY: ios-aot-sim-build
ios-aot-sim-build: ios-aot-sim
	@echo "Building iOS AOT app for simulator..."
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile-AOT-Sim.xcodeproj \
		-scheme SdfViewerMobile-AOT-Sim \
		-configuration Debug \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
		build

# Run iOS AOT app in iPad simulator
.PHONY: ios-aot-sim-run
ios-aot-sim-run: ios-aot-sim-build
	@echo "Launching simulator..."
	xcrun simctl boot 'iPad Pro 13-inch (M4)' 2>/dev/null || true
	open -a Simulator
	xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.SdfViewerMobile-AOT-Sim 2>/dev/null || true
	xcrun simctl install 'iPad Pro 13-inch (M4)' $$(find ~/Library/Developer/Xcode/DerivedData -name "SdfViewerMobile-AOT-Sim.app" -path "*/Build/Products/Debug-iphonesimulator/*" ! -path "*/Index.noindex/*" 2>/dev/null | head -1)
	xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.SdfViewerMobile-AOT-Sim


# Generate AOT Device Xcode project
.PHONY: ios-aot-device-project
ios-aot-device-project: build-shaders $(IOS_LOCAL_LIBS)
	@echo "Generating iOS AOT Device Xcode project..."
	cd SdfViewerMobile && ./generate-project.sh project-aot-device.yml
	@echo "Xcode project generated: SdfViewerMobile/SdfViewerMobile.xcodeproj"

# Build iOS AOT for device (incremental - reuses existing builds)
# The build script now handles: jank runtime build, module compilation, library bundling
.PHONY: ios-aot-device
ios-aot-device: build-sdf-deps-standalone build-shaders
	@echo "Building vybe.sdf for iOS Device (AOT)..."
	./SdfViewerMobile/build_ios_jank_aot.sh device
	@echo "Generating iOS Device Xcode project..."
	cd SdfViewerMobile && ./generate-project.sh project-aot-device.yml
	@echo ""
	@echo "============================================"
	@echo "  iOS AOT Device Build Complete!"
	@echo "============================================"

# Build iOS AOT for device (clean rebuild)
.PHONY: ios-aot-device-clean
ios-aot-device-clean: clean-cache ios-aot-device

# Build and run iOS AOT for Device (requires device connected)
# Note: You may need to open Xcode first for signing: open SdfViewerMobile/SdfViewerMobile.xcodeproj
.PHONY: ios-aot-device-run
ios-aot-device-run: ios-aot-device
	@echo "Building iOS app for device..."
	@echo "(If signing fails, open Xcode first: open SdfViewerMobile/SdfViewerMobile-AOT-Device.xcodeproj)"
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile-AOT-Device.xcodeproj \
		-scheme SdfViewerMobile-AOT-Device \
		-configuration Debug \
		-sdk iphoneos \
		-allowProvisioningUpdates \
		build
	@echo ""
	@echo "Installing to connected device..."
	@DEVICE_ID=$$(xcrun devicectl list devices 2>/dev/null | grep -E "connected.*iPad|connected.*iPhone" | awk '{print $$3}' | head -1); \
	if [ -z "$$DEVICE_ID" ]; then \
		echo "No iOS device found. Connect your device and try again."; \
		exit 1; \
	fi; \
	APP_PATH=$$(find ~/Library/Developer/Xcode/DerivedData -name "SdfViewerMobile-AOT-Device.app" -path "*/Debug-iphoneos/*" ! -path "*/Index.noindex/*" 2>/dev/null | head -1); \
	echo "Installing $$APP_PATH to device $$DEVICE_ID..."; \
	xcrun devicectl device install app --device "$$DEVICE_ID" "$$APP_PATH"; \
	echo "Terminating existing app (if running)..."; \
	xcrun devicectl device process terminate --device "$$DEVICE_ID" com.vybe.SdfViewerMobile-AOT-Device 2>/dev/null || true; \
	echo "Launching app..."; \
	xcrun devicectl device process launch --device "$$DEVICE_ID" com.vybe.SdfViewerMobile-AOT-Device

# ============================================================================
# iOS JIT targets (LLVM-based JIT for development)
# ============================================================================
# NOTE: JIT only works when launched from Xcode (requires entitlements)
# Build LLVM first: make ios-jit-llvm-sim (takes ~2 hours)

# Build LLVM for iOS Simulator (one-time, ~2 hours)
# Saves the LLVM commit hash for version checking
ios-jit-llvm-sim:
	@echo "Building LLVM for iOS Simulator (this takes ~2 hours)..."
	cd $(JANK_SRC) && ./bin/build-ios-llvm simulator
	@# Save the LLVM commit hash for version checking
	@cd $(JANK_SRC)/build/llvm && git rev-parse HEAD > $(HOME)/dev/ios-llvm-build/ios-llvm-simulator.commit
	@echo "Saved LLVM commit: $$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-simulator.commit)"

# Build LLVM for iOS Device (one-time, ~2 hours)
# Saves the LLVM commit hash for version checking
ios-jit-llvm-device:
	@echo "Building LLVM for iOS Device (this takes ~2 hours)..."
	cd $(JANK_SRC) && ./bin/build-ios-llvm device
	@# Save the LLVM commit hash for version checking
	@cd $(JANK_SRC)/build/llvm && git rev-parse HEAD > $(HOME)/dev/ios-llvm-build/ios-llvm-device.commit
	@echo "Saved LLVM commit: $$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-device.commit)"

# Check that iOS LLVM version matches current jank LLVM (simulator)
.PHONY: check-ios-llvm-version-sim
check-ios-llvm-version-sim:
	@if [ ! -f "$(HOME)/dev/ios-llvm-build/ios-llvm-simulator.commit" ]; then \
		echo ""; \
		echo "ERROR: iOS LLVM for simulator has not been built yet."; \
		echo "       Run: make ios-jit-llvm-sim (takes ~2 hours)"; \
		echo ""; \
		exit 1; \
	fi
	@IOS_COMMIT=$$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-simulator.commit); \
	JANK_COMMIT=$$(cd $(JANK_SRC)/build/llvm && git rev-parse HEAD); \
	if [ "$$IOS_COMMIT" != "$$JANK_COMMIT" ]; then \
		echo ""; \
		echo "ERROR: iOS LLVM version mismatch!"; \
		echo "       iOS LLVM commit:  $$IOS_COMMIT"; \
		echo "       jank LLVM commit: $$JANK_COMMIT"; \
		echo ""; \
		echo "The PCH will be incompatible. Rebuild iOS LLVM:"; \
		echo "       make ios-jit-llvm-sim (takes ~2 hours)"; \
		echo ""; \
		exit 1; \
	fi
	@echo "iOS LLVM version OK: $$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-simulator.commit | cut -c1-12)"

# Check that iOS LLVM version matches current jank LLVM (device)
.PHONY: check-ios-llvm-version-device
check-ios-llvm-version-device:
	@if [ ! -f "$(HOME)/dev/ios-llvm-build/ios-llvm-device.commit" ]; then \
		echo ""; \
		echo "ERROR: iOS LLVM for device has not been built yet."; \
		echo "       Run: make ios-jit-llvm-device (takes ~2 hours)"; \
		echo ""; \
		exit 1; \
	fi
	@IOS_COMMIT=$$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-device.commit); \
	JANK_COMMIT=$$(cd $(JANK_SRC)/build/llvm && git rev-parse HEAD); \
	if [ "$$IOS_COMMIT" != "$$JANK_COMMIT" ]; then \
		echo ""; \
		echo "ERROR: iOS LLVM version mismatch!"; \
		echo "       iOS LLVM commit:  $$IOS_COMMIT"; \
		echo "       jank LLVM commit: $$JANK_COMMIT"; \
		echo ""; \
		echo "The PCH will be incompatible. Rebuild iOS LLVM:"; \
		echo "       make ios-jit-llvm-device (takes ~2 hours)"; \
		echo ""; \
		exit 1; \
	fi
	@echo "iOS LLVM version OK: $$(cat $(HOME)/dev/ios-llvm-build/ios-llvm-device.commit | cut -c1-12)"

# Build jank with JIT for iOS Simulator
# Depends on check-ios-llvm-version-sim to ensure PCH compatibility
ios-jit-sim: check-ios-llvm-version-sim
	@echo "Building jank with JIT for iOS Simulator..."
	cd $(JANK_SRC) && ./bin/build-ios build-ios-sim-jit Debug simulator jit
	@echo ""
	@echo "============================================"
	@echo "  iOS Simulator JIT Build Complete!"
	@echo "============================================"
	@echo ""
	@echo "Libraries at: $(JANK_SRC)/build-ios-sim-jit/"
	@echo ""
	@echo "NOTE: JIT only works when app is launched from Xcode."
	@echo "Sign with ios/jank-jit.entitlements for development."

# Build jank with JIT for iOS Device
ios-jit-device:
	@echo "Building jank with JIT for iOS Device..."
	cd $(JANK_SRC) && ./bin/build-ios build-ios-device-jit Debug device jit
	@echo ""
	@echo "============================================"
	@echo "  iOS Device JIT Build Complete!"
	@echo "============================================"
	@echo ""
	@echo "Libraries at: $(JANK_SRC)/build-ios-device-jit/"
	@echo ""
	@echo "NOTE: JIT only works when app is launched from Xcode."
	@echo "Sign with ios/jank-jit.entitlements for development."

# Sync jank source files to iOS JIT bundle (copies entire vybe directory)
.PHONY: ios-jit-sync-sources
ios-jit-sync-sources:
	@echo "Syncing jank source files to iOS bundle..."
	@mkdir -p SdfViewerMobile/jank-resources/src/jank/vybe
	@rsync -av --delete --include='*.jank' --include='*/' --exclude='*' src/vybe/ SdfViewerMobile/jank-resources/src/jank/vybe/
	@# Copy .clang-format for native-source formatting in JIT mode (from build directory)
	@if [ -f "$(JANK_SRC)/build-ios-sim-jit/.clang-format" ]; then \
		cp $(JANK_SRC)/build-ios-sim-jit/.clang-format SdfViewerMobile/jank-resources/.clang-format; \
	elif [ -f "$(JANK_SRC)/build-ios-jit-simulator/.clang-format" ]; then \
		cp $(JANK_SRC)/build-ios-jit-simulator/.clang-format SdfViewerMobile/jank-resources/.clang-format; \
	elif [ -f "$(JANK_SRC)/build/.clang-format" ]; then \
		cp $(JANK_SRC)/build/.clang-format SdfViewerMobile/jank-resources/.clang-format; \
	else \
		echo "Warning: .clang-format not found in JANK_SRC build directories"; \
	fi
	@echo "Sources synced!"

# Sync jank include headers to iOS JIT bundle (all third-party headers needed for JIT)
.PHONY: ios-jit-sync-includes
ios-jit-sync-includes:
	@echo "Syncing jank include headers to iOS bundle..."
	@# Create target directories first (rsync doesn't create nested paths)
	@mkdir -p SdfViewerMobile/jank-resources/include/gc
	@mkdir -p SdfViewerMobile/jank-resources/include/immer
	@mkdir -p SdfViewerMobile/jank-resources/include/folly
	@mkdir -p SdfViewerMobile/jank-resources/include/boost
	@mkdir -p SdfViewerMobile/jank-resources/include/jank
	@mkdir -p SdfViewerMobile/jank-resources/include/jtl
	@mkdir -p SdfViewerMobile/jank-resources/include/clojure
	@# GC headers from bdwgc (gc/gc_cpp.h, etc.)
	@rsync -av --delete $(JANK_SRC)/third-party/bdwgc/include/gc/ SdfViewerMobile/jank-resources/include/gc/
	@cp -f $(JANK_SRC)/third-party/bdwgc/include/gc.h SdfViewerMobile/jank-resources/include/
	@cp -f $(JANK_SRC)/third-party/bdwgc/include/gc_cpp.h SdfViewerMobile/jank-resources/include/
	@# immer headers (immer/heap/heap_policy.hpp, etc.) - NOTE: source is immer/immer/, target is immer/
	@rsync -av --delete $(JANK_SRC)/third-party/immer/immer/ SdfViewerMobile/jank-resources/include/immer/
	@# folly headers (folly/SharedMutex.h, etc.) - NOTE: source is folly/folly/, target is folly/
	@rsync -av --delete $(JANK_SRC)/third-party/folly/folly/ SdfViewerMobile/jank-resources/include/folly/
	@# boost headers (merge boost-preprocessor and boost-multiprecision into boost/)
	@rsync -av $(JANK_SRC)/third-party/boost-preprocessor/include/boost/ SdfViewerMobile/jank-resources/include/boost/
	@rsync -av $(JANK_SRC)/third-party/boost-multiprecision/include/boost/ SdfViewerMobile/jank-resources/include/boost/
	@# stduuid header (uuid.h)
	@cp -f $(JANK_SRC)/third-party/stduuid/include/uuid.h SdfViewerMobile/jank-resources/include/
	@# jank headers (jank/runtime/object.hpp, etc.)
	@rsync -av --delete $(JANK_SRC)/include/cpp/jank/ SdfViewerMobile/jank-resources/include/jank/
	@rsync -av --delete $(JANK_SRC)/include/cpp/jtl/ SdfViewerMobile/jank-resources/include/jtl/
	@# clojure native headers (from include/cpp/clojure/, NOT src/cpp/clojure/)
	@rsync -av --delete $(JANK_SRC)/include/cpp/clojure/ SdfViewerMobile/jank-resources/include/clojure/
	@# Clang builtin headers (needed for JIT - stddef.h, stdarg.h, etc.)
	@mkdir -p SdfViewerMobile/jank-resources/clang/include
	@rsync -av --delete $(JANK_SRC)/build/llvm-install/usr/local/lib/clang/*/include/ SdfViewerMobile/jank-resources/clang/include/
	@echo "Includes synced!"

# Build precompiled header for iOS JIT (rebuilds when jank headers change)
.PHONY: ios-jit-pch
ios-jit-pch: ios-jit-sync-includes
	@echo "Building iOS JIT precompiled header (simulator)..."
	./SdfViewerMobile/build-ios-pch.sh simulator

# Build PCH for device
.PHONY: ios-jit-pch-device
ios-jit-pch-device: ios-jit-sync-includes
	@echo "Building iOS JIT precompiled header (device)..."
	./SdfViewerMobile/build-ios-pch.sh device

# Sync jank library to iOS JIT bundle (rebuilds if source changed)
.PHONY: ios-jit-sync-lib
ios-jit-sync-lib:
	@echo "Rebuilding and syncing jank library for iOS Simulator JIT..."
	@mkdir -p SdfViewerMobile/build-iphonesimulator-jit
	@ninja -C $(JANK_SRC)/build-ios-sim-jit libjank.a
	@cp $(JANK_SRC)/build-ios-sim-jit/libjank.a SdfViewerMobile/build-iphonesimulator-jit/
	@cp $(JANK_SRC)/build-ios-sim-jit/libjankzip.a SdfViewerMobile/build-iphonesimulator-jit/
	@cp $(JANK_SRC)/build-ios-sim-jit/third-party/bdwgc/libgc.a SdfViewerMobile/build-iphonesimulator-jit/
	@echo "Library synced!"

# Build AOT-compiled core libs for device JIT (clojure.core, clojure.string, etc.)
# This enables hybrid mode: AOT core libs + JIT user code
# The AOT .o files will be detected by weak symbols in sdf_viewer_ios.mm
.PHONY: ios-jit-device-core-aot
ios-jit-device-core-aot:
	@echo "Building AOT core libs for iOS device JIT..."
	@echo "This compiles clojure.core, clojure.string, etc. for fast startup."
	@echo ""
	@echo "Step 1: Generate and cross-compile core libraries..."
	@mkdir -p SdfViewerMobile/build-iphoneos-jit/core-aot
	@# Run ios-bundle in device mode without entry-module to get just core libs
	@# This uses the native jank compiler to generate C++ and cross-compiles for iOS
	cd $(JANK_SRC) && ./bin/ios-bundle \
		--build-dir $(PWD)/SdfViewerMobile/build-iphoneos-jit/ios-bundle-build \
		--output-dir $(PWD)/SdfViewerMobile/build-iphoneos-jit/core-aot \
		--skip-build \
		device
	@echo ""
	@echo "Step 2: Copy .o files to build directory..."
	@cp SdfViewerMobile/build-iphoneos-jit/core-aot/*.o SdfViewerMobile/build-iphoneos-jit/ 2>/dev/null || true
	@echo ""
	@echo "AOT core libs built!"
	@echo "Files in SdfViewerMobile/build-iphoneos-jit/core-aot/:"
	@ls -la SdfViewerMobile/build-iphoneos-jit/core-aot/*.o 2>/dev/null || echo "  (no .o files yet)"
	@echo ""
	@echo "To enable hybrid mode:"
	@echo "  1. Add the .o files to your Xcode project"
	@echo "  2. Or update project-jit-device.yml to include them"
	@echo "  3. Rebuild: make ios-jit-device-run"

# Build AOT modules for SIMULATOR JIT (same as device, just different target)
# Rebuilds if any .jank file is newer than the library
# Depends on build-sdf-deps-standalone because ios-bundle needs libsdf_deps.dylib for JIT symbol resolution
.PHONY: ios-jit-sim-aot
ios-jit-sim-aot: build-sdf-deps-standalone
	@if [ -n "$$(find src -name '*.jank' -newer SdfViewerMobile/build-iphonesimulator/libvybe_aot.a 2>/dev/null)" ] || \
	   [ ! -f "SdfViewerMobile/build-iphonesimulator/libvybe_aot.a" ] || \
	   [ ! -f "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ] || \
	   [ "$(JANK_SRC)/bin/ios-bundle" -nt "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
		echo "Source files or ios-bundle changed - rebuilding AOT library for simulator..."; \
		./SdfViewerMobile/build_ios_jank_aot.sh simulator; \
	else \
		echo "Simulator AOT library up to date."; \
	fi

# Build JIT-only core libs (no app modules - they're loaded via remote compile server)
# Note: Always runs ninja which handles incrementality properly via source dependencies
# ios-jit-sim-core depends on ios-jit-sim to ensure libjank.a is built first
# ios-jit-sim runs ninja which handles incrementality via source/header dependencies
.PHONY: ios-jit-sim-core
ios-jit-sim-core: ios-jit-sim
	@echo "Building JIT-only core libs for simulator..."
	@# Clean old generated files to avoid stale ABI issues
	@rm -rf SdfViewerMobile/build-iphonesimulator-jit/obj 2>/dev/null || true
	@rm -rf SdfViewerMobile/build-iphonesimulator-jit/generated 2>/dev/null || true
	@./SdfViewerMobile/build_ios_jank_jit.sh simulator

# Create libjank_aot.a from locally generated object files
# NOTE: ios-bundle --jit already generates all files into SdfViewerMobile/build-iphonesimulator-jit/
# We just need to create the combined static library for Xcode
.PHONY: ios-jit-sim-core-libs
ios-jit-sim-core-libs: ios-jit-sim-core
	@echo "Creating libjank_aot.a from locally generated object files..."
	@# Clean old libs to avoid stale ABI issues
	@rm -f SdfViewerMobile/build-iphonesimulator-jit/libjank_aot.a 2>/dev/null || true
	@rm -f SdfViewerMobile/build-iphonesimulator-jit/libfolly.a 2>/dev/null || true
	@if [ ! -f "SdfViewerMobile/build-iphonesimulator-jit/libjank.a" ]; then \
		echo "ERROR: JIT-only libraries not found!"; \
		echo "Run 'make ios-jit-sim-core' first."; \
		exit 1; \
	fi
	@# Create libjank_aot.a from locally generated core libs + nREPL server + jank_aot_init.o
	@ar -crs SdfViewerMobile/build-iphonesimulator-jit/libjank_aot.a \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_core_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_set_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_string_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_walk_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_template_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/clojure_test_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/jank_nrepl_server_server_generated.o \
		SdfViewerMobile/build-iphonesimulator-jit/obj/jank_aot_init.o
	@# Copy libfolly.a from jank build (ios-bundle doesn't copy it)
	@if [ -f "$(JANK_SRC)/build-ios-sim-jit/libfolly.a" ]; then \
		cp $(JANK_SRC)/build-ios-sim-jit/libfolly.a SdfViewerMobile/build-iphonesimulator-jit/; \
	elif [ -f "$(JANK_SRC)/build-ios-jit-simulator/libfolly.a" ]; then \
		cp $(JANK_SRC)/build-ios-jit-simulator/libfolly.a SdfViewerMobile/build-iphonesimulator-jit/; \
	else \
		echo "WARNING: libfolly.a not found in jank build directories"; \
	fi
	@# Create merged LLVM library if it doesn't exist
	@if [ ! -f "SdfViewerMobile/build-iphonesimulator-jit/libllvm_merged.a" ]; then \
		echo "Creating merged LLVM library (this may take a minute)..."; \
		libtool -static -o SdfViewerMobile/build-iphonesimulator-jit/libllvm_merged.a \
			$$HOME/dev/ios-llvm-build/ios-llvm-simulator/lib/*.a 2>/dev/null || true; \
	fi
	@echo "JIT-only simulator libraries copied!"
	@echo ""
	@echo "NOTE: App namespaces are NOT included."
	@echo "They will be loaded via remote compile server at runtime."

# Copy simulator JIT libraries (mirrors ios-jit-device-libs exactly)
.PHONY: ios-jit-sim-libs
ios-jit-sim-libs: ios-jit-sim-aot
	@echo "Copying simulator JIT libraries..."
	@if [ ! -f "$(JANK_SRC)/build-ios-sim-jit/libjank.a" ]; then \
		echo "ERROR: Simulator JIT libraries not found!"; \
		echo "Run 'make ios-jit-sim' first to build them."; \
		exit 1; \
	fi
	@mkdir -p SdfViewerMobile/build-iphonesimulator-jit/generated
	@cp $(JANK_SRC)/build-ios-sim-jit/libjank.a SdfViewerMobile/build-iphonesimulator-jit/
	@cp $(JANK_SRC)/build-ios-sim-jit/libjankzip.a SdfViewerMobile/build-iphonesimulator-jit/
	@cp $(JANK_SRC)/build-ios-sim-jit/third-party/bdwgc/libgc.a SdfViewerMobile/build-iphonesimulator-jit/
	@cp $(JANK_SRC)/build-ios-sim-jit/libfolly.a SdfViewerMobile/build-iphonesimulator-jit/
	@# Create merged LLVM library if it doesn't exist or iOS LLVM was rebuilt
	@LLVM_COMMIT_FILE="$$HOME/dev/ios-llvm-build/ios-llvm-simulator.commit"; \
	MERGED_LIB="SdfViewerMobile/build-iphonesimulator-jit/libllvm_merged.a"; \
	if [ ! -f "$$MERGED_LIB" ]; then \
		echo "Creating merged LLVM library (this may take a minute)..."; \
		libtool -static -o "$$MERGED_LIB" \
			$$HOME/dev/ios-llvm-build/ios-llvm-simulator/lib/*.a 2>/dev/null; \
	elif [ -f "$$LLVM_COMMIT_FILE" ] && [ "$$LLVM_COMMIT_FILE" -nt "$$MERGED_LIB" ]; then \
		echo "iOS LLVM was rebuilt, recreating merged LLVM library..."; \
		rm -f "$$MERGED_LIB"; \
		libtool -static -o "$$MERGED_LIB" \
			$$HOME/dev/ios-llvm-build/ios-llvm-simulator/lib/*.a 2>/dev/null; \
	fi
	@# Copy jank_aot_init.cpp from AOT build (generated by ios-bundle via ios-jit-sim-aot)
	@# This contains proper module loading: core libs -> nREPL asio -> user modules
	@echo "Copying jank_aot_init.cpp from AOT build..."
	@if [ -f "SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp" ]; then \
		cp SdfViewerMobile/build-iphonesimulator/generated/jank_aot_init.cpp SdfViewerMobile/build-iphonesimulator-jit/generated/; \
		echo "jank_aot_init.cpp copied!"; \
	else \
		echo "ERROR: jank_aot_init.cpp not found - run ios-jit-sim-aot first"; \
		exit 1; \
	fi
	@echo "Simulator JIT libraries copied!"

# ============================================================================
# JIT Simulator Mode (remote compile server for app namespaces)
# ============================================================================

# Generate JIT Xcode project for simulator
.PHONY: ios-jit-sim-project
ios-jit-sim-project: ios-jit-sim-core-libs ios-jit-sync-sources
	@echo "Generating JIT simulator Xcode project..."
	cd SdfViewerMobile && ./generate-project.sh project-jit-sim.yml
	@echo "Project generated!"
	@echo ""
	@echo "NOTE: App namespaces will be loaded via remote compile server at runtime."

# Build JIT simulator app
.PHONY: ios-jit-sim-build
ios-jit-sim-build: ios-jit-sync-sources ios-jit-pch ios-jit-sim-project
	@echo "Building iOS JIT app for simulator..."
	@# Clean Xcode's cached object files that might depend on jank headers
	@# This ensures changes to rtti.hpp, prelude.hpp, etc. are picked up
	@rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-Sim-*/Build/Intermediates.noindex/SdfViewerMobile-JIT-Sim.build/Debug-iphonesimulator/SdfViewerMobile-JIT-Sim.build/Objects-normal/arm64/jank_aot_init.* 2>/dev/null || true
	@rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-Sim-*/Build/Intermediates.noindex/SdfViewerMobile-JIT-Sim.build/Debug-iphonesimulator/SdfViewerMobile-JIT-Sim.build/Objects-normal/arm64/sdf_viewer_ios.* 2>/dev/null || true
	@# Touch main source file to trigger rebuild if headers changed
	@touch SdfViewerMobile/sdf_viewer_ios.mm 2>/dev/null || true
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile-JIT-Sim.xcodeproj \
		-scheme SdfViewerMobile-JIT-Sim \
		-configuration Debug \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
		build

# Build, install and run iOS JIT app in simulator (auto-starts compile server)
.PHONY: ios-jit-sim-run
ios-jit-sim-run: ios-jit-sim-build ios-compile-server-sim
	@echo "Launching simulator..."
	xcrun simctl boot 'iPad Pro 13-inch (M4)' 2>/dev/null || true
	open -a Simulator
	xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.SdfViewerMobile-JIT-Sim 2>/dev/null || true
	xcrun simctl install 'iPad Pro 13-inch (M4)' $$(find ~/Library/Developer/Xcode/DerivedData -name "SdfViewerMobile-JIT-Sim.app" -path "*/Build/Products/Debug-iphonesimulator/*" ! -path "*/Index.noindex/*" 2>/dev/null | head -1)
	xcrun simctl launch --console-pty 'iPad Pro 13-inch (M4)' com.vybe.SdfViewerMobile-JIT-Sim

# ============================================================================
# iOS JIT Device Targets (remote compile server for app namespaces)
# ============================================================================

# Build JIT core libs for device
.PHONY: ios-jit-device-core
ios-jit-device-core:
	@echo "Building JIT core libs for device (ninja handles incrementality)..."
	@# Clean old generated files to avoid stale ABI issues
	@rm -rf SdfViewerMobile/build-iphoneos-jit/obj 2>/dev/null || true
	@rm -rf SdfViewerMobile/build-iphoneos-jit/generated 2>/dev/null || true
	@./SdfViewerMobile/build_ios_jank_jit.sh device

# Copy JIT device libraries (no app modules - uses remote compile server)
# Note: ios-bundle puts everything in SdfViewerMobile/build-iphoneos-jit/ already
.PHONY: ios-jit-device-core-libs
ios-jit-device-core-libs: ios-jit-device-core
	@echo "Device JIT libraries already in place from ios-bundle..."
	@# Clean old libs to avoid stale ABI issues
	@rm -f SdfViewerMobile/build-iphoneos-jit/libjank_aot.a 2>/dev/null || true
	@rm -f SdfViewerMobile/build-iphoneos-jit/libfolly.a 2>/dev/null || true
	@# ios-bundle already creates libjank_aot.a with core libs + JIT init
	@# Just need to copy libfolly.a and create libllvm_merged.a
	@if [ -f "$(JANK_SRC)/build-ios-jit-device/libfolly.a" ]; then \
		cp $(JANK_SRC)/build-ios-jit-device/libfolly.a SdfViewerMobile/build-iphoneos-jit/; \
	elif [ -f "$(JANK_SRC)/build-ios-device-jit/libfolly.a" ]; then \
		cp $(JANK_SRC)/build-ios-device-jit/libfolly.a SdfViewerMobile/build-iphoneos-jit/; \
	else \
		echo "WARNING: libfolly.a not found"; \
	fi
	@# Create merged LLVM library if it doesn't exist
	@if [ ! -f "SdfViewerMobile/build-iphoneos-jit/libllvm_merged.a" ]; then \
		echo "Creating merged LLVM library (this may take a minute)..."; \
		libtool -static -o SdfViewerMobile/build-iphoneos-jit/libllvm_merged.a \
			$$HOME/dev/ios-llvm-build/ios-llvm-device/lib/*.a 2>/dev/null; \
	fi
	@echo "Device JIT libraries ready!"

# Generate device JIT Xcode project
.PHONY: ios-jit-device-project
ios-jit-device-project: ios-jit-device-core-libs ios-jit-sync-sources
	@echo "Generating JIT device Xcode project..."
	cd SdfViewerMobile && ./generate-project.sh project-jit-device.yml
	@echo "Project generated!"
	@echo ""
	@echo "NOTE: App namespaces will be loaded via remote compile server at runtime."

# Build JIT device app
.PHONY: ios-jit-device-build
ios-jit-device-build: ios-jit-sync-sources ios-jit-pch-device ios-jit-device-project
	@echo "Building iOS JIT app for device..."
	@echo "(If signing fails, open Xcode first: open SdfViewerMobile/SdfViewerMobile-JIT-Device.xcodeproj)"
	@# Clean Xcode's cached object files that might depend on jank headers
	@# This ensures changes to rtti.hpp, prelude.hpp, etc. are picked up
	@rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-Device-*/Build/Intermediates.noindex/SdfViewerMobile-JIT-Device.build/Debug-iphoneos/SdfViewerMobile-JIT-Device.build/Objects-normal/arm64/jank_aot_init.* 2>/dev/null || true
	@rm -rf ~/Library/Developer/Xcode/DerivedData/SdfViewerMobile-JIT-Device-*/Build/Intermediates.noindex/SdfViewerMobile-JIT-Device.build/Debug-iphoneos/SdfViewerMobile-JIT-Device.build/Objects-normal/arm64/sdf_viewer_ios.* 2>/dev/null || true
	@# Touch main source file to trigger rebuild if headers changed
	@touch SdfViewerMobile/sdf_viewer_ios.mm 2>/dev/null || true
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile-JIT-Device.xcodeproj \
		-scheme SdfViewerMobile-JIT-Device \
		-configuration Debug \
		-sdk iphoneos \
		-allowProvisioningUpdates \
		build

# Build, install and run iOS JIT app on device (auto-starts compile server)
.PHONY: ios-jit-device-run
ios-jit-device-run: ios-compile-server-device ios-device-nrepl-proxy ios-jit-device-build
	@echo ""
	@echo "Installing to connected device..."
	@DEVICE_ID=$$(xcrun devicectl list devices 2>/dev/null | grep -E "connected.*iPad|connected.*iPhone" | awk '{print $$3}' | head -1); \
	if [ -z "$$DEVICE_ID" ]; then \
		echo "ERROR: No iOS device found. Connect your device and try again."; \
		exit 1; \
	fi; \
	APP_PATH=$$(find ~/Library/Developer/Xcode/DerivedData -name "SdfViewerMobile-JIT-Device.app" -path "*/Debug-iphoneos/*" ! -path "*/Index.noindex/*" 2>/dev/null | head -1); \
	if [ -z "$$APP_PATH" ]; then \
		echo "ERROR: Built app not found!"; \
		exit 1; \
	fi; \
	echo "Installing $$APP_PATH to device $$DEVICE_ID..."; \
	xcrun devicectl device install app --device "$$DEVICE_ID" "$$APP_PATH"; \
	echo "Terminating existing app (if running)..."; \
	xcrun devicectl device process terminate --device "$$DEVICE_ID" com.vybe.SdfViewerMobile-JIT-Device 2>/dev/null || true; \
	echo "Launching app..."; \
	xcrun devicectl device process launch --device "$$DEVICE_ID" com.vybe.SdfViewerMobile-JIT-Device; \
	echo ""; \
	echo "═══════════════════════════════════════════════════════════════════════"; \
	echo "  App launched! Compile server running on port 5571."; \
	echo "  Connect nREPL to localhost:5559"; \
	echo "  Press Ctrl+C to stop compile server and exit."; \
	echo "═══════════════════════════════════════════════════════════════════════"; \
	echo ""; \
	echo "Compile server logs:"; \
	tail -f /dev/null

# Compile server for iOS Device JIT development (port 5571)
# Always restarts to pick up latest code changes
.PHONY: ios-compile-server-device
ios-compile-server-device:
	@echo "Starting compile server for device (port 5571)..."
	@-pkill -f "compile-server.*--port 5571" 2>/dev/null || true
	@sleep 0.2
	@cd $(JANK_SRC) && ./build/compile-server --target device --port 5571 \
		--module-path $(PWD)/SdfViewerMobile/jank-resources/src/jank:$(JANK_SRC)/../nrepl-server/src/jank \
		--jit-lib /opt/homebrew/lib/libvulkan.dylib \
		--jit-lib /opt/homebrew/lib/libSDL3.dylib \
		--jit-lib /opt/homebrew/lib/libshaderc_shared.dylib \
		--jit-lib $(PWD)/vulkan/libsdf_deps.dylib \
		-I $(PWD)/SdfViewerMobile/jank-resources/include \
		-I /opt/homebrew/include \
		-I /opt/homebrew/include/SDL3 \
		-I $(PWD) \
		-I $(PWD)/vendor \
		-I $(PWD)/vendor/imgui \
		-I $(PWD)/vendor/imgui/backends \
		-I $(PWD)/vendor/flecs/distr \
		-I $(PWD)/vendor/miniaudio & \
	echo "Waiting for compile server to be ready..."; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		sleep 0.5; \
		if lsof -i :5571 >/dev/null 2>&1; then \
			echo "Compile server ready!"; \
			break; \
		fi; \
	done

# Start iproxy to forward nREPL from device to Mac
# Device nREPL runs on port 5558, Mac listens on 5559
# Runs in background if not already running
.PHONY: ios-device-nrepl-proxy
ios-device-nrepl-proxy:
	@if ! lsof -i :5559 >/dev/null 2>&1; then \
		echo "Starting iproxy: Mac:5559 -> Device:5558 (background)"; \
		iproxy 5559 5558 & \
		sleep 0.5; \
		echo "nREPL proxy started. Connect to localhost:5559"; \
	else \
		echo "iproxy already running on port 5559"; \
	fi

# ============================================================================
# End iOS JIT Device Targets
# ============================================================================
