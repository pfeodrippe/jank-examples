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
	@echo "  make sdf-clean   - Run SDF viewer with fresh cache (like sdf but clean)"
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
	@echo ""
	@echo "iOS builds:"
	@echo "  make sdf-ios-simulator-run - Build and run iOS app in iPad simulator"
	@echo "  make sdf-ios-device-run    - Build and run iOS app on connected device"
	@echo "  make ios-setup    - Download/build iOS dependencies (MoltenVK, SDL3)"
	@echo "  make ios-project  - Generate Xcode project (requires xcodegen)"
	@echo "  make ios-build    - Build iOS app"
	@echo "  make ios-clean    - Clean iOS build artifacts"

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
	-I$(JANK_SRC)/third-party/bpptree/include \
	-I$(JANK_SRC)/third-party/folly \
	-I$(JANK_SRC)/third-party/boost-multiprecision/include \
	-I$(JANK_SRC)/third-party/boost-preprocessor/include \
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

.PHONY: ios-setup ios-project ios-build ios-clean ios-runtime ios-core-libs sdf-ios sdf-ios-run sdf-ios-sim-run sdf-ios-simulator-run sdf-ios-device sdf-ios-device-run

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
	cd SdfViewerMobile && xcodegen generate
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

# Quick iOS jank C++ generation and cross-compilation (like sdf-clean for iOS)
sdf-ios: clean-cache build-sdf-deps ios-jank ios-project
	@echo ""
	@echo "============================================"
	@echo "  iOS Build Complete!"
	@echo "============================================"
	@echo ""
	@echo "Libraries: SdfViewerMobile/build/"
	@echo "Objects:   SdfViewerMobile/build/obj/"
	@echo "Project:   SdfViewerMobile/SdfViewerMobile.xcodeproj"
	@echo ""
	@echo "To run in simulator: make sdf-ios-run"

# Build iOS for simulator (sdf-ios builds for device, this builds for simulator)
.PHONY: sdf-ios-sim
sdf-ios-sim: clean-cache build-sdf-deps ios-sim-runtime ios-sim-core-libs
	@echo "Building vybe.sdf for iOS Simulator..."
	./SdfViewerMobile/build_ios_jank_aot.sh simulator
	$(MAKE) ios-sim-copy-libs
	$(MAKE) ios-project
	@echo ""
	@echo "============================================"
	@echo "  iOS Simulator Build Complete!"
	@echo "============================================"

# Run iOS app in iPad simulator
# Build and run for iOS Simulator
sdf-ios-sim-run: sdf-ios-sim
	@echo "Building iOS app for simulator..."
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile.xcodeproj \
		-scheme SdfViewerMobile \
		-configuration Debug \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' \
		build
	@echo ""
	@echo "Launching simulator..."
	xcrun simctl boot 'iPad Pro 13-inch (M4)' 2>/dev/null || true
	open -a Simulator
	xcrun simctl install 'iPad Pro 13-inch (M4)' $$(find SdfViewerMobile -name "SdfViewerMobile.app" -path "*/Debug-iphonesimulator/*" | head -1)
	xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.SdfViewerMobile

# Aliases for backwards compatibility
sdf-ios-run: sdf-ios-sim-run
sdf-ios-simulator-run: sdf-ios-sim-run

# Build iOS for device
.PHONY: sdf-ios-device
sdf-ios-device: clean-cache build-sdf-deps ios-runtime ios-core-libs
	@echo "Building vybe.sdf for iOS Device..."
	./SdfViewerMobile/build_ios_jank_aot.sh device
	$(MAKE) ios-copy-libs
	$(MAKE) ios-project
	@echo ""
	@echo "============================================"
	@echo "  iOS Device Build Complete!"
	@echo "============================================"

# Build and run for iOS Device (requires device connected)
# Note: You may need to open Xcode first for signing: open SdfViewerMobile/SdfViewerMobile.xcodeproj
sdf-ios-device-run: sdf-ios-device
	@echo "Building iOS app for device..."
	@echo "(If signing fails, open Xcode first: open SdfViewerMobile/SdfViewerMobile.xcodeproj)"
	cd SdfViewerMobile && xcodebuild \
		-project SdfViewerMobile.xcodeproj \
		-scheme SdfViewerMobile \
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
	APP_PATH=$$(find ~/Library/Developer/Xcode/DerivedData -name "SdfViewerMobile.app" -path "*/Debug-iphoneos/*" 2>/dev/null | head -1); \
	echo "Installing $$APP_PATH to device $$DEVICE_ID..."; \
	xcrun devicectl device install app --device "$$DEVICE_ID" "$$APP_PATH"; \
	echo "Launching app..."; \
	xcrun devicectl device process launch --device "$$DEVICE_ID" com.vybe.SdfViewerMobile
