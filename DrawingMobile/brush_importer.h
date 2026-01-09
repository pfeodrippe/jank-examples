// brush_importer.h - Procreate .brush/.brushset file importer
// Handles ZIP extraction, plist parsing, and texture loading

#ifndef BRUSH_IMPORTER_H
#define BRUSH_IMPORTER_H

#import <Foundation/Foundation.h>

// Brush settings parsed from Brush.archive
typedef struct {
    float spacing;          // 0.0-1.0, how often brush stamps
    float sizeJitter;       // 0.0-1.0, random size variation
    float opacityJitter;    // 0.0-1.0, random opacity variation
    float scatterAmount;    // 0.0-1.0, position scatter
    float rotationJitter;   // 0-360 degrees
    float sizePressure;     // -1.0 to 1.0, pressure affects size
    float opacityPressure;  // -1.0 to 1.0, pressure affects opacity
    float grainScale;       // Texture scale multiplier
    float hardness;         // Edge hardness 0.0-1.0
    float flow;             // Paint flow 0.0-1.0
    int shapeInverted;      // 0 = normal, 1 = inverted (Procreate shapeInverted property)
} ProcreateBrushSettings;

// Imported brush data
typedef struct {
    int32_t brushId;
    char name[256];
    char thumbnailPath[1024];
    int32_t shapeTextureId;     // Metal texture ID, -1 if none
    int32_t grainTextureId;     // Metal texture ID, -1 if none
    int32_t thumbnailTextureId; // Preview thumbnail texture ID, -1 if none
    int32_t thumbnailWidth;     // Thumbnail image width
    int32_t thumbnailHeight;    // Thumbnail image height
    ProcreateBrushSettings settings;
} ImportedBrush;

@interface BrushImporter : NSObject

// Import single .brush file
// Returns brush ID (>0) on success, -1 on failure
+ (int32_t)importBrushFromURL:(NSURL*)url;

// Import .brushset file (multiple brushes)
// Returns array of brush IDs
+ (NSArray<NSNumber*>*)importBrushSetFromURL:(NSURL*)url;

// Get list of all imported brushes
+ (NSArray<NSDictionary*>*)getImportedBrushes;

// Get brush by ID
+ (ImportedBrush*)getBrushById:(int32_t)brushId;

// Apply brush settings to Metal renderer
+ (BOOL)applyBrush:(int32_t)brushId;

// Delete imported brush
+ (BOOL)deleteBrush:(int32_t)brushId;

// Get brush library directory
+ (NSURL*)brushLibraryDirectory;

// Load bundled brushset from app resources
+ (NSArray<NSNumber*>*)loadBundledBrushSet:(NSString*)bundleName;

// Load brushset from absolute path
+ (NSArray<NSNumber*>*)loadBrushSetFromPath:(NSString*)path;

// Load brushes from a bundled zip file containing .brushset and/or .brush files
+ (NSArray<NSNumber*>*)loadBundledBrushZip:(NSString*)zipName;

@end

#endif // BRUSH_IMPORTER_H
