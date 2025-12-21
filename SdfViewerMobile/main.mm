// SDF Viewer Mobile - iOS entry point
// Uses SDL3 + Vulkan (via MoltenVK) for rendering
// This allows maximum code reuse with the macOS version

#import <UIKit/UIKit.h>
#import <SDL3/SDL.h>
#import <SDL3/SDL_main.h>

// Forward declaration of our main SDF viewer function
extern "C" int sdf_viewer_main(int argc, char* argv[]);

// SDL3 requires we define SDL_main (or use SDL_MAIN_USE_CALLBACKS)
int main(int argc, char* argv[]) {
    @autoreleasepool {
        // Initialize SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            NSLog(@"Failed to initialize SDL: %s", SDL_GetError());
            return 1;
        }

        // Run the SDF viewer
        int result = sdf_viewer_main(argc, argv);

        // Cleanup
        SDL_Quit();

        return result;
    }
}
