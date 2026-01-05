// Drawing Mobile - iOS entry point
// Uses SDL3 + Metal for rendering (via SDL_Renderer)
// This allows maximum code reuse with the macOS version

#import <UIKit/UIKit.h>
#import <SDL3/SDL.h>
#import <SDL3/SDL_main.h>

// Forward declaration of our main drawing function
extern "C" int drawing_mobile_main(int argc, char* argv[]);

// =============================================================================
// Force-reference Metal stamp symbols to prevent dead stripping by linker
// These symbols need to be exported for the JIT to find them at runtime
// =============================================================================
extern "C" {
    bool metal_stamp_init(void* sdl_window, int width, int height);
    void metal_stamp_cleanup();
    bool metal_stamp_is_available();
    void metal_stamp_set_brush_size(float size);
    void metal_stamp_set_brush_hardness(float hardness);
    void metal_stamp_set_brush_opacity(float opacity);
    void metal_stamp_set_brush_spacing(float spacing);
    void metal_stamp_set_brush_color(float r, float g, float b, float a);
    void metal_stamp_begin_stroke(float x, float y, float pressure);
    void metal_stamp_add_stroke_point(float x, float y, float pressure);
    void metal_stamp_end_stroke();
    void metal_stamp_cancel_stroke();
    void metal_stamp_clear_canvas(float r, float g, float b, float a);
    void metal_stamp_render_stroke();
    void metal_stamp_present();
}

// This function is never called, but forces the linker to keep these symbols
__attribute__((used)) static void force_export_metal_symbols() {
    volatile void* symbols[] = {
        (void*)metal_stamp_init,
        (void*)metal_stamp_cleanup,
        (void*)metal_stamp_is_available,
        (void*)metal_stamp_set_brush_size,
        (void*)metal_stamp_set_brush_hardness,
        (void*)metal_stamp_set_brush_opacity,
        (void*)metal_stamp_set_brush_spacing,
        (void*)metal_stamp_set_brush_color,
        (void*)metal_stamp_begin_stroke,
        (void*)metal_stamp_add_stroke_point,
        (void*)metal_stamp_end_stroke,
        (void*)metal_stamp_cancel_stroke,
        (void*)metal_stamp_clear_canvas,
        (void*)metal_stamp_render_stroke,
        (void*)metal_stamp_present,
    };
    (void)symbols;
}

// SDL3 requires we define SDL_main (or use SDL_MAIN_USE_CALLBACKS)
int main(int argc, char* argv[]) {
    @autoreleasepool {
        // Initialize SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            NSLog(@"Failed to initialize SDL: %s", SDL_GetError());
            return 1;
        }

        // Run the drawing app
        int result = drawing_mobile_main(argc, argv);

        // Cleanup
        SDL_Quit();

        return result;
    }
}
