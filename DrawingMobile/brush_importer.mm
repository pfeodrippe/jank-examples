// brush_importer.mm - Procreate .brush/.brushset file importer implementation

#import "brush_importer.h"
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <iostream>
#include <vector>
#include <map>
#include <zlib.h>

// External Metal texture loading functions
extern "C" {
    int32_t metal_stamp_load_texture_data(const uint8_t* data, int width, int height);
    void metal_stamp_set_brush_size(float size);
    void metal_stamp_set_brush_hardness(float hardness);
    void metal_stamp_set_brush_opacity(float opacity);
    void metal_stamp_set_brush_spacing(float spacing);
    void metal_stamp_set_brush_scatter(float scatter);
    void metal_stamp_set_brush_size_jitter(float jitter);
    void metal_stamp_set_brush_opacity_jitter(float jitter);
    void metal_stamp_set_brush_rotation_jitter(float degrees);
    void metal_stamp_set_brush_grain_scale(float scale);
    void metal_stamp_set_brush_size_pressure(float pressure);
    void metal_stamp_set_brush_opacity_pressure(float pressure);
    void metal_stamp_set_brush_shape_texture(int32_t textureId);
    void metal_stamp_set_brush_grain_texture(int32_t textureId);
}

// Global brush storage
static std::map<int32_t, ImportedBrush> g_importedBrushes;
static int32_t g_nextBrushId = 1;

@implementation BrushImporter

+ (NSURL*)brushLibraryDirectory {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSURL* docsDir = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL* brushDir = [docsDir URLByAppendingPathComponent:@"Brushes" isDirectory:YES];

    if (![fm fileExistsAtPath:brushDir.path]) {
        NSError* error;
        [fm createDirectoryAtURL:brushDir withIntermediateDirectories:YES attributes:nil error:&error];
        if (error) {
            NSLog(@"[BrushImporter] Failed to create brush directory: %@", error);
        }
    }

    return brushDir;
}

+ (NSURL*)extractBrushArchive:(NSURL*)brushURL toDirectory:(NSURL*)destDir {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* error;

    // Create unique extraction directory
    NSString* brushName = [[brushURL lastPathComponent] stringByDeletingPathExtension];
    NSURL* extractDir = [destDir URLByAppendingPathComponent:brushName isDirectory:YES];

    // Remove existing directory if present
    if ([fm fileExistsAtPath:extractDir.path]) {
        [fm removeItemAtURL:extractDir error:nil];
    }

    [fm createDirectoryAtURL:extractDir withIntermediateDirectories:YES attributes:nil error:&error];
    if (error) {
        NSLog(@"[BrushImporter] Failed to create extraction directory: %@", error);
        return nil;
    }

    // Copy the brush file and rename to .zip for extraction
    NSURL* zipURL = [extractDir URLByAppendingPathComponent:@"brush.zip"];
    [fm copyItemAtURL:brushURL toURL:zipURL error:&error];
    if (error) {
        NSLog(@"[BrushImporter] Failed to copy brush file: %@", error);
        return nil;
    }

    // Use NSFileManager to unzip (iOS 16+) or manual extraction
    // For older iOS, we'll use a simpler approach with coordination

    // Try using Archive utility via NSTask equivalent
    // Actually, let's use SSZipArchive if available, or implement manual extraction

    // For now, use a simpler approach: the brush file IS a zip, just extract it
    NSURL* contentsDir = [extractDir URLByAppendingPathComponent:@"contents" isDirectory:YES];
    [fm createDirectoryAtURL:contentsDir withIntermediateDirectories:YES attributes:nil error:nil];

    // Use Foundation's built-in unarchiver for ZIP files
    // This requires iOS 16+ for native ZIP support
    // For compatibility, we'll use a manual approach with NSData

    NSData* zipData = [NSData dataWithContentsOfURL:zipURL];
    if (!zipData) {
        NSLog(@"[BrushImporter] Failed to read ZIP data");
        return nil;
    }

    // For iOS, we can use the file coordinator with ZIP type
    // Extract ZIP contents using our manual ZIP parser
    if (![self extractZipFile:zipURL toDirectory:contentsDir]) {
        NSLog(@"[BrushImporter] Failed to extract ZIP contents");
        return nil;
    }

    // Clean up zip file
    [fm removeItemAtURL:zipURL error:nil];

    return contentsDir;
}

+ (BOOL)extractZipFile:(NSURL*)zipURL toDirectory:(NSURL*)destDir {
    // Use NSFileManager with file coordination for iOS-native ZIP extraction
    // This is a simplified implementation - for production, use a proper ZIP library

    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* error;

    // Read the ZIP file
    NSData* zipData = [NSData dataWithContentsOfURL:zipURL options:0 error:&error];
    if (!zipData || error) {
        NSLog(@"[BrushImporter] Cannot read ZIP: %@", error);
        return NO;
    }

    const uint8_t* bytes = (const uint8_t*)[zipData bytes];
    NSUInteger length = [zipData length];

    // Basic ZIP parsing - look for local file headers (PK\x03\x04)
    NSUInteger offset = 0;

    while (offset + 30 < length) {
        // Check for local file header signature
        if (bytes[offset] != 0x50 || bytes[offset+1] != 0x4B ||
            bytes[offset+2] != 0x03 || bytes[offset+3] != 0x04) {
            // Not a local file header, might be central directory
            break;
        }

        // Parse local file header
        uint16_t flags = bytes[offset+6] | (bytes[offset+7] << 8);
        uint16_t compression = bytes[offset+8] | (bytes[offset+9] << 8);
        uint32_t compressedSize = bytes[offset+18] | (bytes[offset+19] << 8) |
                                  (bytes[offset+20] << 16) | (bytes[offset+21] << 24);
        uint32_t uncompressedSize = bytes[offset+22] | (bytes[offset+23] << 8) |
                                    (bytes[offset+24] << 16) | (bytes[offset+25] << 24);
        uint16_t nameLength = bytes[offset+26] | (bytes[offset+27] << 8);
        uint16_t extraLength = bytes[offset+28] | (bytes[offset+29] << 8);

        if (offset + 30 + nameLength > length) break;

        NSString* fileName = [[NSString alloc] initWithBytes:bytes+offset+30
                                                     length:nameLength
                                                   encoding:NSUTF8StringEncoding];

        offset += 30 + nameLength + extraLength;

        if (offset + compressedSize > length) break;

        // Skip directories
        if ([fileName hasSuffix:@"/"]) {
            NSURL* dirURL = [destDir URLByAppendingPathComponent:fileName isDirectory:YES];
            [fm createDirectoryAtURL:dirURL withIntermediateDirectories:YES attributes:nil error:nil];
            offset += compressedSize;
            continue;
        }

        // Create parent directories
        NSString* parentDir = [fileName stringByDeletingLastPathComponent];
        if (parentDir.length > 0) {
            NSURL* parentURL = [destDir URLByAppendingPathComponent:parentDir isDirectory:YES];
            [fm createDirectoryAtURL:parentURL withIntermediateDirectories:YES attributes:nil error:nil];
        }

        NSURL* fileURL = [destDir URLByAppendingPathComponent:fileName];

        // Extract file data
        NSData* fileData;
        if (compression == 0) {
            // Stored (no compression)
            fileData = [NSData dataWithBytes:bytes+offset length:compressedSize];
        } else if (compression == 8) {
            // Deflate compression - use zlib
            NSData* compressedData = [NSData dataWithBytes:bytes+offset length:compressedSize];
            fileData = [self inflateData:compressedData expectedSize:uncompressedSize];
        } else {
            NSLog(@"[BrushImporter] Unsupported compression method: %d", compression);
            offset += compressedSize;
            continue;
        }

        if (fileData) {
            [fileData writeToURL:fileURL atomically:YES];
            NSLog(@"[BrushImporter] Extracted: %@", fileName);
        }

        offset += compressedSize;
    }

    return YES;
}

+ (NSData*)inflateData:(NSData*)compressedData expectedSize:(uint32_t)expectedSize {
    // Use zlib to decompress deflate data
    if (compressedData.length == 0) return [NSData data];

    NSMutableData* decompressed = [NSMutableData dataWithLength:expectedSize];

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    stream.next_in = (Bytef*)[compressedData bytes];
    stream.avail_in = (uInt)[compressedData length];
    stream.next_out = (Bytef*)[decompressed mutableBytes];
    stream.avail_out = expectedSize;

    // Use raw deflate (negative windowBits)
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return nil;
    }

    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (result != Z_STREAM_END && result != Z_OK) {
        NSLog(@"[BrushImporter] Inflate failed with result: %d", result);
        return nil;
    }

    [decompressed setLength:stream.total_out];
    return decompressed;
}

+ (int32_t)importBrushFromURL:(NSURL*)url {
    NSLog(@"[BrushImporter] Importing brush from: %@", url.path);

    // Extract brush archive
    NSURL* libraryDir = [self brushLibraryDirectory];
    NSURL* extractedDir = [self extractBrushArchive:url toDirectory:libraryDir];

    if (!extractedDir) {
        NSLog(@"[BrushImporter] Failed to extract brush archive");
        return -1;
    }

    NSFileManager* fm = [NSFileManager defaultManager];

    // Look for Brush.archive (settings)
    NSURL* brushArchiveURL = [extractedDir URLByAppendingPathComponent:@"Brush.archive"];

    // Look for Shape.png and Grain.png
    NSURL* shapeURL = [extractedDir URLByAppendingPathComponent:@"Shape.png"];
    NSURL* grainURL = [extractedDir URLByAppendingPathComponent:@"Grain.png"];

    // Look for thumbnail
    NSURL* thumbnailURL = [extractedDir URLByAppendingPathComponent:@"QuickLook/Thumbnail.png"];
    if (![fm fileExistsAtPath:thumbnailURL.path]) {
        thumbnailURL = [extractedDir URLByAppendingPathComponent:@"Thumbnail.png"];
    }

    // Create brush entry
    ImportedBrush brush;
    memset(&brush, 0, sizeof(brush));
    brush.brushId = g_nextBrushId++;
    brush.shapeTextureId = -1;
    brush.grainTextureId = -1;

    // Set name from filename
    NSString* name = [[url lastPathComponent] stringByDeletingPathExtension];
    strncpy(brush.name, [name UTF8String], sizeof(brush.name) - 1);

    // Set thumbnail path
    if ([fm fileExistsAtPath:thumbnailURL.path]) {
        strncpy(brush.thumbnailPath, [thumbnailURL.path UTF8String], sizeof(brush.thumbnailPath) - 1);
    }

    // Load textures
    if ([fm fileExistsAtPath:shapeURL.path]) {
        brush.shapeTextureId = [self loadTextureFromURL:shapeURL];
        NSLog(@"[BrushImporter] Loaded shape texture: %d", brush.shapeTextureId);
    }

    if ([fm fileExistsAtPath:grainURL.path]) {
        brush.grainTextureId = [self loadTextureFromURL:grainURL];
        NSLog(@"[BrushImporter] Loaded grain texture: %d", brush.grainTextureId);
    }

    // Parse settings from Brush.archive
    if ([fm fileExistsAtPath:brushArchiveURL.path]) {
        [self parseBrushArchive:brushArchiveURL settings:&brush.settings name:brush.name nameBufferSize:sizeof(brush.name)];
    } else {
        // Use default settings
        brush.settings.spacing = 0.1f;
        brush.settings.sizeJitter = 0.0f;
        brush.settings.opacityJitter = 0.0f;
        brush.settings.scatterAmount = 0.0f;
        brush.settings.rotationJitter = 0.0f;
        brush.settings.sizePressure = 0.8f;
        brush.settings.opacityPressure = 0.3f;
        brush.settings.grainScale = 1.0f;
        brush.settings.hardness = 0.5f;
        brush.settings.flow = 1.0f;
    }

    // Store brush
    g_importedBrushes[brush.brushId] = brush;

    NSLog(@"[BrushImporter] Imported brush '%s' with ID %d", brush.name, brush.brushId);
    return brush.brushId;
}

+ (int32_t)loadTextureFromURL:(NSURL*)url {
    UIImage* image = [UIImage imageWithContentsOfFile:url.path];
    if (!image) return -1;

    CGImageRef cgImage = image.CGImage;
    NSUInteger width = CGImageGetWidth(cgImage);
    NSUInteger height = CGImageGetHeight(cgImage);

    // Create grayscale context
    NSUInteger bytesPerPixel = 1;
    NSUInteger bytesPerRow = width * bytesPerPixel;
    uint8_t* rawData = (uint8_t*)calloc(height * bytesPerRow, sizeof(uint8_t));

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
    CGContextRef context = CGBitmapContextCreate(rawData, width, height,
                                                  8, bytesPerRow, colorSpace,
                                                  kCGImageAlphaNone);
    CGColorSpaceRelease(colorSpace);

    if (!context) {
        free(rawData);
        return -1;
    }

    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
    CGContextRelease(context);

    // Load into Metal
    int32_t textureId = metal_stamp_load_texture_data(rawData, (int)width, (int)height);

    free(rawData);
    return textureId;
}

+ (BOOL)parseBrushArchive:(NSURL*)archiveURL settings:(ProcreateBrushSettings*)settings name:(char*)nameBuffer nameBufferSize:(size_t)nameBufferSize {
    NSData* data = [NSData dataWithContentsOfURL:archiveURL];
    if (!data) return NO;

    NSError* error;
    NSDictionary* plist = [NSPropertyListSerialization propertyListWithData:data
                                                                   options:NSPropertyListImmutable
                                                                    format:nil
                                                                     error:&error];
    if (error || !plist) {
        NSLog(@"[BrushImporter] Failed to parse Brush.archive: %@", error);
        return NO;
    }

    // Brush.archive uses NSKeyedArchiver format
    // The actual brush data is in $objects array, index 1
    NSArray* objects = plist[@"$objects"];
    if (!objects || objects.count < 2) {
        NSLog(@"[BrushImporter] No $objects array in archive");
        return NO;
    }

    // The main brush object is typically at index 1
    id brushObj = objects[1];
    if (![brushObj isKindOfClass:[NSDictionary class]]) {
        NSLog(@"[BrushImporter] Brush object is not a dictionary");
        return NO;
    }

    NSDictionary* brush = (NSDictionary*)brushObj;

    // Extract brush name from referenced object
    if (brush[@"name"]) {
        // name is a reference to another object in $objects
        id nameRef = brush[@"name"];
        if ([nameRef respondsToSelector:@selector(intValue)]) {
            NSInteger nameIndex = [nameRef intValue];
            if (nameIndex > 0 && nameIndex < (NSInteger)objects.count) {
                id nameObj = objects[nameIndex];
                if ([nameObj isKindOfClass:[NSString class]]) {
                    if (nameBuffer && nameBufferSize > 0) {
                        strncpy(nameBuffer, [(NSString*)nameObj UTF8String], nameBufferSize - 1);
                        nameBuffer[nameBufferSize - 1] = '\0';
                    }
                }
            }
        }
    }

    // Map Procreate keys to our settings (based on reverse-engineering)
    // Spacing
    if (brush[@"plotSpacing"]) {
        settings->spacing = [brush[@"plotSpacing"] floatValue];
        // Procreate spacing is very small (0.008), scale it up for our renderer
        if (settings->spacing < 0.05f) settings->spacing *= 10.0f;
    }

    // Size jitter
    if (brush[@"dynamicsJitterSize"]) {
        settings->sizeJitter = [brush[@"dynamicsJitterSize"] floatValue];
    }

    // Opacity jitter
    if (brush[@"dynamicsJitterOpacity"]) {
        settings->opacityJitter = [brush[@"dynamicsJitterOpacity"] floatValue];
    }

    // Scatter/position jitter
    if (brush[@"plotJitter"]) {
        settings->scatterAmount = [brush[@"plotJitter"] floatValue];
    }

    // Size pressure response
    if (brush[@"dynamicsPressureSize"]) {
        settings->sizePressure = [brush[@"dynamicsPressureSize"] floatValue];
    }

    // Opacity pressure response
    if (brush[@"dynamicsPressureOpacity"]) {
        settings->opacityPressure = [brush[@"dynamicsPressureOpacity"] floatValue];
    }

    // Grain/texture depth
    if (brush[@"grainDepth"]) {
        settings->grainScale = [brush[@"grainDepth"] floatValue];
    }

    // Flow (glazed flow)
    if (brush[@"dynamicsGlazedFlow"]) {
        settings->flow = [brush[@"dynamicsGlazedFlow"] floatValue];
    }

    // Hardness - not directly available, estimate from other settings
    settings->hardness = 0.5f;

    NSLog(@"[BrushImporter] Parsed settings: spacing=%.4f, sizeJitter=%.2f, opacityJitter=%.2f, scatter=%.2f, sizePressure=%.2f, opacityPressure=%.2f, grainScale=%.2f, flow=%.2f",
          settings->spacing, settings->sizeJitter, settings->opacityJitter, settings->scatterAmount,
          settings->sizePressure, settings->opacityPressure, settings->grainScale, settings->flow);

    return YES;
}

+ (NSArray<NSNumber*>*)importBrushSetFromURL:(NSURL*)url {
    // TODO: Implement brushset import (multiple brushes)
    NSMutableArray* brushIds = [NSMutableArray array];

    // Extract the brushset
    NSURL* libraryDir = [self brushLibraryDirectory];
    NSURL* extractedDir = [self extractBrushArchive:url toDirectory:libraryDir];

    if (!extractedDir) return @[];

    // Find all brush subdirectories
    NSFileManager* fm = [NSFileManager defaultManager];
    NSArray* contents = [fm contentsOfDirectoryAtURL:extractedDir
                          includingPropertiesForKeys:@[NSURLIsDirectoryKey]
                                             options:0
                                               error:nil];

    for (NSURL* item in contents) {
        NSNumber* isDir;
        [item getResourceValue:&isDir forKey:NSURLIsDirectoryKey error:nil];

        if ([isDir boolValue]) {
            // Check if it contains Brush.archive
            NSURL* brushArchive = [item URLByAppendingPathComponent:@"Brush.archive"];
            if ([fm fileExistsAtPath:brushArchive.path]) {
                // This is a brush folder, import it
                int32_t brushId = [self importBrushFromExtractedDirectory:item];
                if (brushId > 0) {
                    [brushIds addObject:@(brushId)];
                }
            }
        }
    }

    return brushIds;
}

+ (int32_t)importBrushFromExtractedDirectory:(NSURL*)dir {
    NSFileManager* fm = [NSFileManager defaultManager];

    // Create brush entry
    ImportedBrush brush;
    memset(&brush, 0, sizeof(brush));
    brush.brushId = g_nextBrushId++;
    brush.shapeTextureId = -1;
    brush.grainTextureId = -1;

    // Set initial name from directory name (will be overwritten by Brush.archive if available)
    NSString* name = [dir lastPathComponent];
    strncpy(brush.name, [name UTF8String], sizeof(brush.name) - 1);

    // Look for thumbnail
    NSURL* thumbnailURL = [dir URLByAppendingPathComponent:@"QuickLook/Thumbnail.png"];
    if ([fm fileExistsAtPath:thumbnailURL.path]) {
        strncpy(brush.thumbnailPath, [thumbnailURL.path UTF8String], sizeof(brush.thumbnailPath) - 1);
    }

    // Load textures - check root level first, then Sub01 folder
    NSURL* shapeURL = [dir URLByAppendingPathComponent:@"Shape.png"];
    NSURL* grainURL = [dir URLByAppendingPathComponent:@"Grain.png"];

    // Check Sub01 folder if not found at root
    if (![fm fileExistsAtPath:shapeURL.path]) {
        NSURL* sub01ShapeURL = [dir URLByAppendingPathComponent:@"Sub01/Shape.png"];
        if ([fm fileExistsAtPath:sub01ShapeURL.path]) {
            shapeURL = sub01ShapeURL;
        }
    }
    if (![fm fileExistsAtPath:grainURL.path]) {
        NSURL* sub01GrainURL = [dir URLByAppendingPathComponent:@"Sub01/Grain.png"];
        if ([fm fileExistsAtPath:sub01GrainURL.path]) {
            grainURL = sub01GrainURL;
        }
    }

    if ([fm fileExistsAtPath:shapeURL.path]) {
        brush.shapeTextureId = [self loadTextureFromURL:shapeURL];
        NSLog(@"[BrushImporter] Loaded shape texture: %d from %@", brush.shapeTextureId, shapeURL.lastPathComponent);
    }
    if ([fm fileExistsAtPath:grainURL.path]) {
        brush.grainTextureId = [self loadTextureFromURL:grainURL];
        NSLog(@"[BrushImporter] Loaded grain texture: %d from %@", brush.grainTextureId, grainURL.lastPathComponent);
    }

    // Parse settings (will also extract brush name)
    NSURL* archiveURL = [dir URLByAppendingPathComponent:@"Brush.archive"];
    if ([fm fileExistsAtPath:archiveURL.path]) {
        [self parseBrushArchive:archiveURL settings:&brush.settings name:brush.name nameBufferSize:sizeof(brush.name)];
    } else {
        // Use default settings
        brush.settings.spacing = 0.1f;
        brush.settings.sizeJitter = 0.0f;
        brush.settings.opacityJitter = 0.0f;
        brush.settings.scatterAmount = 0.0f;
        brush.settings.rotationJitter = 0.0f;
        brush.settings.sizePressure = 0.8f;
        brush.settings.opacityPressure = 0.3f;
        brush.settings.grainScale = 1.0f;
        brush.settings.hardness = 0.5f;
        brush.settings.flow = 1.0f;
    }

    // Store brush
    g_importedBrushes[brush.brushId] = brush;

    NSLog(@"[BrushImporter] Imported brush '%s' with ID %d (shape=%d, grain=%d)",
          brush.name, brush.brushId, brush.shapeTextureId, brush.grainTextureId);

    return brush.brushId;
}

+ (NSArray<NSDictionary*>*)getImportedBrushes {
    NSMutableArray* brushes = [NSMutableArray array];

    for (auto& pair : g_importedBrushes) {
        ImportedBrush& brush = pair.second;
        NSDictionary* dict = @{
            @"id": @(brush.brushId),
            @"name": [NSString stringWithUTF8String:brush.name],
            @"thumbnailPath": [NSString stringWithUTF8String:brush.thumbnailPath],
            @"hasShapeTexture": @(brush.shapeTextureId >= 0),
            @"hasGrainTexture": @(brush.grainTextureId >= 0)
        };
        [brushes addObject:dict];
    }

    return brushes;
}

+ (ImportedBrush*)getBrushById:(int32_t)brushId {
    auto it = g_importedBrushes.find(brushId);
    if (it == g_importedBrushes.end()) return nullptr;
    return &it->second;
}

+ (BOOL)applyBrush:(int32_t)brushId {
    ImportedBrush* brush = [self getBrushById:brushId];
    if (!brush) return NO;

    // Apply settings to Metal renderer
    metal_stamp_set_brush_spacing(brush->settings.spacing);
    metal_stamp_set_brush_size_jitter(brush->settings.sizeJitter);
    metal_stamp_set_brush_opacity_jitter(brush->settings.opacityJitter);
    metal_stamp_set_brush_scatter(brush->settings.scatterAmount);
    metal_stamp_set_brush_rotation_jitter(brush->settings.rotationJitter);
    metal_stamp_set_brush_size_pressure(brush->settings.sizePressure);
    metal_stamp_set_brush_opacity_pressure(brush->settings.opacityPressure);
    metal_stamp_set_brush_grain_scale(brush->settings.grainScale);
    metal_stamp_set_brush_hardness(brush->settings.hardness);
    // Note: flow is not currently supported in metal_renderer

    // Apply textures
    if (brush->shapeTextureId >= 0) {
        metal_stamp_set_brush_shape_texture(brush->shapeTextureId);
    }
    if (brush->grainTextureId >= 0) {
        metal_stamp_set_brush_grain_texture(brush->grainTextureId);
    }

    NSLog(@"[BrushImporter] Applied brush '%s'", brush->name);
    return YES;
}

+ (BOOL)deleteBrush:(int32_t)brushId {
    auto it = g_importedBrushes.find(brushId);
    if (it == g_importedBrushes.end()) return NO;

    g_importedBrushes.erase(it);
    return YES;
}

+ (NSArray<NSNumber*>*)loadBundledBrushSet:(NSString*)bundleName {
    // Look for brushset in the app bundle
    NSBundle* mainBundle = [NSBundle mainBundle];

    // Try to find as a resource
    NSURL* brushSetURL = [mainBundle URLForResource:bundleName withExtension:@"brushset"];

    if (!brushSetURL) {
        // Try looking in the bundle's resource path directly
        NSString* resourcePath = [mainBundle resourcePath];
        NSString* brushSetPath = [resourcePath stringByAppendingPathComponent:
                                  [NSString stringWithFormat:@"%@.brushset", bundleName]];
        if ([[NSFileManager defaultManager] fileExistsAtPath:brushSetPath]) {
            brushSetURL = [NSURL fileURLWithPath:brushSetPath];
        }
    }

    if (!brushSetURL) {
        NSLog(@"[BrushImporter] Bundled brushset '%@' not found", bundleName);
        return @[];
    }

    NSLog(@"[BrushImporter] Loading bundled brushset from: %@", brushSetURL.path);
    return [self importBrushSetFromURL:brushSetURL];
}

+ (NSArray<NSNumber*>*)loadBrushSetFromPath:(NSString*)path {
    NSURL* brushSetURL = [NSURL fileURLWithPath:path];

    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        NSLog(@"[BrushImporter] Brushset not found at path: %@", path);
        return @[];
    }

    NSLog(@"[BrushImporter] Loading brushset from: %@", path);
    return [self importBrushSetFromURL:brushSetURL];
}

@end
