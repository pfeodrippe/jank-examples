// SDF Viewer iOS - Native C++ implementation using sdf_engine.hpp
// This uses the same Vulkan/MoltenVK rendering as macOS
//
// For jank AOT support, see the build_ios_jank.sh script which will
// generate the jank-powered version once iOS AOT compilation is ready.

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <iostream>
#include <string>

// Include the SDF engine directly (header-only Vulkan renderer)
#include "../vulkan/sdf_engine.hpp"

// Get the iOS bundle resource path
static std::string getBundleResourcePath() {
    NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
    return std::string([bundlePath UTF8String]);
}

// C API for main entry point (called from main.mm)
extern "C" int sdf_viewer_main(int argc, char* argv[]) {
    @autoreleasepool {
        std::cout << std::endl;
        std::cout << "============================================" << std::endl;
        std::cout << "   SDF Viewer Mobile - iOS Edition" << std::endl;
        std::cout << "============================================" << std::endl;
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
