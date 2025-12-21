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
#include <jank/runtime/behavior/callable.hpp>
#pragma pop_macro("nil")

// =============================================================================
// C++ Bridge Functions for jank
// These are the real implementations that override the weak stubs in AOT code
// =============================================================================

static std::string g_resource_path;
static std::string g_shader_dir;

// Get the iOS bundle resource path
static std::string getBundleResourcePath() {
    NSString* bundlePath = [[NSBundle mainBundle] resourcePath];
    return std::string([bundlePath UTF8String]);
}

extern "C" {

// Lifecycle
bool vybe_ios_init_str(std::string const& shader_dir) {
    std::cout << "[vybe_ios] init_str: " << shader_dir << std::endl;
    g_resource_path = getBundleResourcePath();
    g_shader_dir = g_resource_path + "/" + shader_dir;

    if (!sdfx::init(g_shader_dir.c_str())) {
        std::cerr << "[vybe_ios] Failed to initialize SDF engine" << std::endl;
        return false;
    }

    sdfx::set_continuous_mode(true);
    return true;
}

void vybe_ios_cleanup() {
    std::cout << "[vybe_ios] cleanup" << std::endl;
    sdfx::cleanup();
}

bool vybe_ios_should_close() {
    return sdfx::should_close();
}

// Rendering
void vybe_ios_poll_events() {
    sdfx::poll_events_only();
}

void vybe_ios_update_uniforms(float dt) {
    sdfx::update_uniforms(dt);
}

void vybe_ios_draw_frame() {
    sdfx::draw_frame();
}

void vybe_ios_imgui_begin() {
    sdfx::imgui_begin_frame();
}

void vybe_ios_imgui_end() {
    sdfx::imgui_end_frame();
}

// Camera getters
float vybe_ios_get_camera_distance() {
    return sdfx::get_camera_distance();
}

float vybe_ios_get_camera_angle_x() {
    return sdfx::get_camera_angle_x();
}

float vybe_ios_get_camera_angle_y() {
    return sdfx::get_camera_angle_y();
}

float vybe_ios_get_camera_target_y() {
    return sdfx::get_camera_target_y();
}

// Camera setters
void vybe_ios_set_camera_distance(float v) {
    sdfx::set_camera_distance(v);
}

void vybe_ios_set_camera_angle_x(float v) {
    sdfx::set_camera_angle_x(v);
}

void vybe_ios_set_camera_angle_y(float v) {
    sdfx::set_camera_angle_y(v);
}

void vybe_ios_set_camera_target_y(float v) {
    sdfx::set_camera_target_y(v);
}

// Shader - returns pointer to static buffer
static std::string g_shader_name_buf;
const char* vybe_ios_get_shader_name_cstr() {
    g_shader_name_buf = sdfx::get_current_shader_name();
    return g_shader_name_buf.c_str();
}

int vybe_ios_get_shader_count() {
    return sdfx::get_shader_count();
}

void vybe_ios_next_shader() {
    int count = sdfx::get_shader_count();
    if (count <= 0) return;
    int current = sdfx::get_current_shader_index();
    int next = (current + 1) % count;
    sdfx::load_shader_at_index(next);
}

void vybe_ios_prev_shader() {
    int count = sdfx::get_shader_count();
    if (count <= 0) return;
    int current = sdfx::get_current_shader_index();
    int prev = (current - 1 + count) % count;
    sdfx::load_shader_at_index(prev);
}

// Time
double vybe_ios_get_time() {
    return sdfx::get_time();
}

} // extern "C"

// =============================================================================
// jank AOT Module Entry Points
// =============================================================================

extern "C" void* jank_load_clojure_core_native();
extern "C" void* jank_load_core();
extern "C" void* jank_load_vybe_sdf_ios();

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

        std::cout << "[jank] Loading vybe.sdf-ios module..." << std::endl;
        jank_load_vybe_sdf_ios();

        std::cout << "[jank] Runtime initialized successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[jank] Error initializing runtime: " << e.what() << std::endl;
        return false;
    }
}

// Call the jank-exported ios-main function
static void call_jank_ios_main() {
    try {
        // Look up the ios-main var using intern (since find_var returns oref now)
        auto var = jank::runtime::__rt_ctx->intern_var("vybe.sdf-ios", "ios-main");
        if (!var.is_ok()) {
            std::cerr << "[jank] Could not find vybe.sdf-ios/ios-main" << std::endl;
            return;
        }

        // Call it using dynamic_call (no args)
        std::cout << "[jank] Calling vybe.sdf-ios/ios-main..." << std::endl;
        jank::runtime::dynamic_call(var.expect_ok()->deref());
    } catch (const std::exception& e) {
        std::cerr << "[jank] Error calling ios-main: " << e.what() << std::endl;
    }
}

// =============================================================================
// Main Entry Point
// =============================================================================

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
            return 1;
        }

        // Call the jank main function
        call_jank_ios_main();

        return 0;
    }
}
