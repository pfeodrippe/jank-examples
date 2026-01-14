// file_manager_ios.mm - iOS file operations implementation
// Uses NSFileManager for Documents directory access

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include "file_manager_ios.h"
#include "vybed_format.hpp"
#include "animation_thread.h"
#include "metal_renderer.h"
#include <string>
#include <vector>

// Static buffers for returned paths
static char g_documentsPath[1024] = {0};
static char g_drawingsPath[1024] = {0};
static char g_savedPath[1024] = {0};
static char g_drawingName[256] = {0};

extern "C" {

const char* ios_get_documents_path(void) {
    if (g_documentsPath[0] == '\0') {
        @autoreleasepool {
            NSArray *paths = NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES);
            NSString *documentsPath = [paths firstObject];
            if (documentsPath) {
                strncpy(g_documentsPath, [documentsPath UTF8String], sizeof(g_documentsPath) - 1);
            }
        }
    }
    return g_documentsPath;
}

const char* ios_get_drawings_path(void) {
    if (g_drawingsPath[0] == '\0') {
        const char* docs = ios_get_documents_path();
        snprintf(g_drawingsPath, sizeof(g_drawingsPath), "%s/Drawings", docs);
    }
    return g_drawingsPath;
}

int ios_ensure_drawings_directory(void) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:ios_get_drawings_path()];
        NSFileManager *fm = [NSFileManager defaultManager];

        if (![fm fileExistsAtPath:path]) {
            NSError *error = nil;
            BOOL success = [fm createDirectoryAtPath:path
                         withIntermediateDirectories:YES
                                          attributes:nil
                                               error:&error];
            if (!success) {
                NSLog(@"[file_manager_ios] Failed to create Drawings directory: %@", error);
                return 0;
            }
            NSLog(@"[file_manager_ios] Created Drawings directory: %@", path);
        }
        return 1;
    }
}

const char* ios_save_drawing(const char* name) {
    @autoreleasepool {
        if (!ios_ensure_drawings_directory()) {
            return NULL;
        }

        // Generate filename
        std::string filename = vybe::drawing::generate_vybed_filename();
        std::string fullPath = std::string(ios_get_drawings_path()) + "/" + filename;

        // Get weave
        auto& weave = animation::getCurrentWeave();

        // TODO: Capture thumbnail from Metal texture
        // For now, save without thumbnail
        bool success = vybe::drawing::save_vybed(fullPath, weave,
                                                  name ? name : "Untitled",
                                                  nullptr, 0);

        if (success) {
            strncpy(g_savedPath, fullPath.c_str(), sizeof(g_savedPath) - 1);
            NSLog(@"[file_manager_ios] Saved drawing to: %s", g_savedPath);
            return g_savedPath;
        }

        return NULL;
    }
}

int ios_load_drawing(const char* path) {
    @autoreleasepool {
        auto& weave = animation::getCurrentWeave();
        std::string name;

        bool success = vybe::drawing::load_vybed(path, weave, name);
        if (success) {
            weave.invalidateAllCaches();

            // Reset undo tree to match loaded weave
            // This ensures we don't have stale strokes from previous session
            metal_stamp_undo_cleanup();
            int numFrames = 1;  // Default to 1 frame
            if (!weave.threads.empty() && !weave.threads[0].frames.empty()) {
                numFrames = (int)weave.threads[0].frames.size();
            }
            if (numFrames < 1) numFrames = 1;
            metal_stamp_undo_init_with_frames(numFrames);

            NSLog(@"[file_manager_ios] Loaded drawing: %s with %d frames", name.c_str(), numFrames);
            return 1;
        }

        return 0;
    }
}

int ios_list_drawings(char** paths, int max_paths) {
    @autoreleasepool {
        NSString *drawingsDir = [NSString stringWithUTF8String:ios_get_drawings_path()];
        NSFileManager *fm = [NSFileManager defaultManager];

        NSError *error = nil;
        NSArray *contents = [fm contentsOfDirectoryAtPath:drawingsDir error:&error];
        if (error) {
            NSLog(@"[file_manager_ios] Failed to list directory: %@", error);
            return 0;
        }

        // Sort by modification date (newest first)
        NSMutableArray *vybedFiles = [NSMutableArray array];
        for (NSString *filename in contents) {
            if ([filename hasSuffix:@".vybed"]) {
                NSString *fullPath = [drawingsDir stringByAppendingPathComponent:filename];
                [vybedFiles addObject:fullPath];
            }
        }

        // Sort by modification date
        [vybedFiles sortUsingComparator:^NSComparisonResult(NSString *path1, NSString *path2) {
            NSDictionary *attrs1 = [fm attributesOfItemAtPath:path1 error:nil];
            NSDictionary *attrs2 = [fm attributesOfItemAtPath:path2 error:nil];
            NSDate *date1 = attrs1[NSFileModificationDate];
            NSDate *date2 = attrs2[NSFileModificationDate];
            return [date2 compare:date1];  // Descending (newest first)
        }];

        int count = (int)MIN((NSUInteger)max_paths, vybedFiles.count);
        if (paths != NULL) {
            for (int i = 0; i < count; i++) {
                paths[i] = strdup([vybedFiles[i] UTF8String]);
            }
        }

        return (int)vybedFiles.count;
    }
}

int ios_delete_drawing(const char* path) {
    @autoreleasepool {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSFileManager *fm = [NSFileManager defaultManager];

        NSError *error = nil;
        BOOL success = [fm removeItemAtPath:nsPath error:&error];
        if (!success) {
            NSLog(@"[file_manager_ios] Failed to delete: %@", error);
            return 0;
        }

        NSLog(@"[file_manager_ios] Deleted: %@", nsPath);
        return 1;
    }
}

int ios_rename_drawing(const char* path, const char* new_name) {
    @autoreleasepool {
        // Load the file, change name, save back
        auto& weave = animation::getCurrentWeave();
        std::string name;

        if (!vybe::drawing::load_vybed(path, weave, name)) {
            return 0;
        }

        // Save with new name (same path)
        if (!vybe::drawing::save_vybed(path, weave, new_name, nullptr, 0)) {
            return 0;
        }

        return 1;
    }
}

const char* ios_get_drawing_name(const char* path) {
    @autoreleasepool {
        vybe::drawing::VybedFileInfo info;
        if (vybe::drawing::load_vybed_info(path, info)) {
            strncpy(g_drawingName, info.name.c_str(), sizeof(g_drawingName) - 1);
            return g_drawingName;
        }
        return NULL;
    }
}

// Forward declarations for metal renderer functions
extern "C" {
    int metal_stamp_capture_snapshot(unsigned char** out_pixels);
    void metal_stamp_free_snapshot(unsigned char* pixels);
    int metal_stamp_get_canvas_width();
    int metal_stamp_get_canvas_height();
}

int ios_capture_thumbnail(unsigned char* buffer, int buffer_size) {
    @autoreleasepool {
        // 1. Capture canvas pixels (RGBA)
        unsigned char* pixels = nullptr;
        int pixelDataSize = metal_stamp_capture_snapshot(&pixels);
        if (!pixels || pixelDataSize == 0) {
            NSLog(@"[Thumbnail] Failed to capture canvas snapshot");
            return 0;
        }

        int canvasWidth = metal_stamp_get_canvas_width();
        int canvasHeight = metal_stamp_get_canvas_height();

        // 2. Create CGImage from pixels
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            pixels,
            canvasWidth,
            canvasHeight,
            8,  // bits per component
            canvasWidth * 4,  // bytes per row
            colorSpace,
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
        );

        if (!context) {
            NSLog(@"[Thumbnail] Failed to create bitmap context");
            CGColorSpaceRelease(colorSpace);
            metal_stamp_free_snapshot(pixels);
            return 0;
        }

        CGImageRef cgImage = CGBitmapContextCreateImage(context);
        CGContextRelease(context);
        CGColorSpaceRelease(colorSpace);
        metal_stamp_free_snapshot(pixels);

        if (!cgImage) {
            NSLog(@"[Thumbnail] Failed to create CGImage");
            return 0;
        }

        // 3. Scale down to thumbnail size (200x160)
        const int thumbWidth = 200;
        const int thumbHeight = 160;

        UIGraphicsBeginImageContextWithOptions(CGSizeMake(thumbWidth, thumbHeight), NO, 1.0);
        CGContextRef thumbContext = UIGraphicsGetCurrentContext();

        // Flip vertically (CGImage origin is bottom-left)
        CGContextTranslateCTM(thumbContext, 0, thumbHeight);
        CGContextScaleCTM(thumbContext, 1.0, -1.0);

        // Draw scaled image
        CGContextDrawImage(thumbContext, CGRectMake(0, 0, thumbWidth, thumbHeight), cgImage);

        UIImage* thumbImage = UIGraphicsGetImageFromCurrentImageContext();
        UIGraphicsEndImageContext();
        CGImageRelease(cgImage);

        if (!thumbImage) {
            NSLog(@"[Thumbnail] Failed to create thumbnail image");
            return 0;
        }

        // 4. Encode as PNG
        NSData* pngData = UIImagePNGRepresentation(thumbImage);
        if (!pngData) {
            NSLog(@"[Thumbnail] Failed to encode PNG");
            return 0;
        }

        int pngSize = (int)[pngData length];
        if (buffer && buffer_size >= pngSize) {
            memcpy(buffer, [pngData bytes], pngSize);
            NSLog(@"[Thumbnail] Captured %dx%d thumbnail, %d bytes PNG", thumbWidth, thumbHeight, pngSize);
            return pngSize;
        }

        // Return required size if buffer too small
        NSLog(@"[Thumbnail] Buffer too small: need %d, have %d", pngSize, buffer_size);
        return pngSize;
    }
}

} // extern "C"
