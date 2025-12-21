// SDF Viewer iOS - Native C++ implementation using sdf_engine.hpp
// This uses the same Vulkan/MoltenVK rendering as macOS
// Now with jank runtime support for iOS AOT compilation

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <iostream>
#include <string>

// Include the SDF engine directly (header-only Vulkan renderer)
#include "../vulkan/sdf_engine.hpp"

// jank runtime headers
// iOS defines 'nil' as a macro which conflicts with jank's object_type::nil
#pragma push_macro("nil")
#undef nil
#include <gc.h>
#include <jank/runtime/context.hpp>
#include <jank/runtime/core.hpp>
#pragma pop_macro("nil")

// jank AOT-compiled module entry points
extern "C" void* jank_load_clojure_core_native();
extern "C" void* jank_load_core();
extern "C" void* jank_load_test_aot();

// Get the iOS bundle resource path
static std::string getBundleResourcePath() {
    NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
    return std::string([bundlePath UTF8String]);
}

// Initialize jank runtime for iOS AOT
static bool init_jank_runtime() {
    try {
        std::cout << "[jank] Initializing Boehm GC..." << std::endl;
        GC_init();

        std::cout << "[jank] Creating runtime context..." << std::endl;
        jank::runtime::__rt_ctx = new (GC_malloc(sizeof(jank::runtime::context)))
            jank::runtime::context{};

        std::cout << "[jank] Loading clojure.core native functions..." << std::endl;
        jank_load_clojure_core_native();

        std::cout << "[jank] Loading clojure.core..." << std::endl;
        jank_load_core();

        std::cout << "[jank] Loading test AOT module..." << std::endl;
        jank_load_test_aot();

        std::cout << "[jank] Runtime initialized successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[jank] Error initializing runtime: " << e.what() << std::endl;
        return false;
    }
}

// C API for main entry point (called from main.mm)
extern "C" int sdf_viewer_main(int argc, char* argv[]) {
    @autoreleasepool {
        std::cout << std::endl;
        std::cout << "============================================" << std::endl;
        std::cout << "   SDF Viewer Mobile - iOS Edition" << std::endl;
        std::cout << "   (with jank AOT runtime)" << std::endl;
        std::cout << "============================================" << std::endl;
        std::cout << std::endl;

        // Initialize jank runtime
        if (!init_jank_runtime()) {
            std::cerr << "Warning: jank runtime initialization failed" << std::endl;
            std::cerr << "Continuing without jank support..." << std::endl;
        }

        std::cout << std::endl;
        std::cout << "Character: Kim Kitsuragi" << std::endl;
        std::cout << "  - Lieutenant, RCM Precinct 57" << std::endl;
        std::cout << "  - Age: 43" << std::endl;
        std::cout << "  - Known for: Orange bomber jacket" << std::endl;
        std::cout << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  Touch drag   = Orbit camera" << std::endl;
        std::cout << "  Pinch        = Zoom in/out" << std::endl;
        std::cout << "  Two-finger   = Pan camera" << std::endl;
        std::cout << std::endl;

        // Get resource paths
        std::string resourcePath = getBundleResourcePath();
        std::string shaderDir = resourcePath + "/vulkan_kim";

        std::cout << "Resource path: " << resourcePath << std::endl;
        std::cout << "Shader dir: " << shaderDir << std::endl;

        // Initialize the SDF engine
        if (!sdfx::init(shaderDir.c_str())) {
            std::cerr << "Failed to initialize SDF engine" << std::endl;
            return 1;
        }

        // Enable continuous rendering for smooth animations
        sdfx::set_continuous_mode(true);

        // Main render loop
        std::cout << "Starting viewer..." << std::endl;
        while (!sdfx::should_close()) {
            // Poll events (touch, gestures via SDL3)
            sdfx::poll_events_only();

            // Update uniforms (time, camera, etc.)
            sdfx::update_uniforms(1.0f / 60.0f);

            // Begin ImGui frame (required before draw_frame)
            sdfx::imgui_begin_frame();

            // Any ImGui UI code would go here
            // For now, just an empty frame

            // End ImGui frame (prepares draw data)
            sdfx::imgui_end_frame();

            // Draw frame (includes ImGui rendering)
            sdfx::draw_frame();
        }

        // Cleanup
        sdfx::cleanup();

        std::cout << std::endl;
        std::cout << "Viewer closed." << std::endl;
        std::cout << "\"I appreciate your discretion, detective.\"" << std::endl;
        return 0;
    }
}
