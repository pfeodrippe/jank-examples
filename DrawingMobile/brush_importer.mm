// brush_importer.mm - Procreate .brush/.brushset file importer implementation

#import "brush_importer.h"
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <iostream>
#include <vector>
#include <map>
#include <zlib.h>
#include <spawn.h>
#include <sys/wait.h>

// External Metal texture loading functions
extern "C" {
    int32_t metal_stamp_load_texture_data(const uint8_t* data, int width, int height);
    int32_t metal_stamp_load_rgba_texture_data(const uint8_t* data, int width, int height);
    void metal_stamp_set_brush_type(int32_t type);
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
    void metal_stamp_set_brush_shape_inverted(int inverted);
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
#if TARGET_OS_SIMULATOR
    // On simulator, use system unzip command via posix_spawn (more reliable)
    extern char **environ;

    pid_t pid;
    const char* args[] = {"/usr/bin/unzip", "-o", "-q",
                          [zipURL.path UTF8String], "-d",
                          [destDir.path UTF8String], NULL};

    int status = posix_spawn(&pid, "/usr/bin/unzip", NULL, NULL, (char* const*)args, environ);
    if (status == 0) {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            NSLog(@"[BrushImporter] Successfully extracted ZIP using system unzip");
            return YES;
        } else {
            NSLog(@"[BrushImporter] System unzip failed with exit status: %d", WEXITSTATUS(status));
        }
    } else {
        NSLog(@"[BrushImporter] posix_spawn failed: %d", status);
    }
    // Fall through to manual extraction if system unzip fails
#endif

    // Manual ZIP extraction for device - use Central Directory for reliable sizes
    NSFileManager* fm = [NSFileManager defaultManager];
    NSError* error;

    NSData* zipData = [NSData dataWithContentsOfURL:zipURL options:0 error:&error];
    if (!zipData || error) {
        NSLog(@"[BrushImporter] Cannot read ZIP: %@", error);
        return NO;
    }

    const uint8_t* bytes = (const uint8_t*)[zipData bytes];
    NSUInteger length = [zipData length];

    // Step 1: Find End of Central Directory record (search backwards for PK\x05\x06)
    NSInteger eocdOffset = -1;
    for (NSInteger i = length - 22; i >= 0 && i >= (NSInteger)length - 65557; i--) {
        if (bytes[i] == 0x50 && bytes[i+1] == 0x4B &&
            bytes[i+2] == 0x05 && bytes[i+3] == 0x06) {
            eocdOffset = i;
            break;
        }
    }

    if (eocdOffset < 0) {
        NSLog(@"[BrushImporter] Cannot find ZIP end of central directory");
        return NO;
    }

    // Read EOCD to find central directory
    uint32_t cdOffset = bytes[eocdOffset+16] | (bytes[eocdOffset+17] << 8) |
                        (bytes[eocdOffset+18] << 16) | (bytes[eocdOffset+19] << 24);
    uint16_t numEntries = bytes[eocdOffset+10] | (bytes[eocdOffset+11] << 8);

    NSLog(@"[BrushImporter] ZIP has %d entries, central dir at offset %u", numEntries, cdOffset);

    // Step 2: Parse Central Directory entries (PK\x01\x02)
    NSUInteger cdPos = cdOffset;
    for (uint16_t i = 0; i < numEntries && cdPos + 46 < length; i++) {
        if (bytes[cdPos] != 0x50 || bytes[cdPos+1] != 0x4B ||
            bytes[cdPos+2] != 0x01 || bytes[cdPos+3] != 0x02) {
            NSLog(@"[BrushImporter] Invalid central directory entry at %lu", cdPos);
            break;
        }

        uint16_t compression = bytes[cdPos+10] | (bytes[cdPos+11] << 8);
        uint32_t compressedSize = bytes[cdPos+20] | (bytes[cdPos+21] << 8) |
                                  (bytes[cdPos+22] << 16) | (bytes[cdPos+23] << 24);
        uint32_t uncompressedSize = bytes[cdPos+24] | (bytes[cdPos+25] << 8) |
                                    (bytes[cdPos+26] << 16) | (bytes[cdPos+27] << 24);
        uint16_t nameLength = bytes[cdPos+28] | (bytes[cdPos+29] << 8);
        uint16_t extraLength = bytes[cdPos+30] | (bytes[cdPos+31] << 8);
        uint16_t commentLength = bytes[cdPos+32] | (bytes[cdPos+33] << 8);
        uint32_t localHeaderOffset = bytes[cdPos+42] | (bytes[cdPos+43] << 8) |
                                     (bytes[cdPos+44] << 16) | (bytes[cdPos+45] << 24);

        if (cdPos + 46 + nameLength > length) break;

        NSString* fileName = [[NSString alloc] initWithBytes:bytes+cdPos+46
                                                     length:nameLength
                                                   encoding:NSUTF8StringEncoding];

        cdPos += 46 + nameLength + extraLength + commentLength;

        // Skip __MACOSX resource forks
        if ([fileName hasPrefix:@"__MACOSX/"] || [fileName containsString:@"/__MACOSX/"]) {
            continue;
        }

        // Skip directories
        if ([fileName hasSuffix:@"/"]) {
            NSURL* dirURL = [destDir URLByAppendingPathComponent:fileName isDirectory:YES];
            [fm createDirectoryAtURL:dirURL withIntermediateDirectories:YES attributes:nil error:nil];
            continue;
        }

        // Parse local file header to find data offset
        if (localHeaderOffset + 30 > length) continue;
        uint16_t localNameLen = bytes[localHeaderOffset+26] | (bytes[localHeaderOffset+27] << 8);
        uint16_t localExtraLen = bytes[localHeaderOffset+28] | (bytes[localHeaderOffset+29] << 8);
        NSUInteger dataOffset = localHeaderOffset + 30 + localNameLen + localExtraLen;

        if (dataOffset + compressedSize > length) continue;

        // Create parent directories
        NSString* parentDir = [fileName stringByDeletingLastPathComponent];
        if (parentDir.length > 0) {
            NSURL* parentURL = [destDir URLByAppendingPathComponent:parentDir isDirectory:YES];
            [fm createDirectoryAtURL:parentURL withIntermediateDirectories:YES attributes:nil error:nil];
        }

        NSURL* fileURL = [destDir URLByAppendingPathComponent:fileName];

        // Extract file data
        NSData* fileData = nil;
        if (compression == 0) {
            fileData = [NSData dataWithBytes:bytes+dataOffset length:compressedSize];
        } else if (compression == 8) {
            NSData* compressedData = [NSData dataWithBytes:bytes+dataOffset length:compressedSize];
            fileData = [self inflateData:compressedData expectedSize:uncompressedSize];
        } else {
            NSLog(@"[BrushImporter] Unsupported compression: %d for %@", compression, fileName);
            continue;
        }

        if (fileData) {
            [fileData writeToURL:fileURL atomically:YES];
            NSLog(@"[BrushImporter] Extracted: %@", fileName);
        }
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
    brush.thumbnailTextureId = -1;
    brush.thumbnailWidth = 0;
    brush.thumbnailHeight = 0;

    // Set name from filename
    NSString* name = [[url lastPathComponent] stringByDeletingPathExtension];
    strncpy(brush.name, [name UTF8String], sizeof(brush.name) - 1);

    // Set thumbnail path and load texture
    if ([fm fileExistsAtPath:thumbnailURL.path]) {
        strncpy(brush.thumbnailPath, [thumbnailURL.path UTF8String], sizeof(brush.thumbnailPath) - 1);
        brush.thumbnailTextureId = [self loadThumbnailFromURL:thumbnailURL
                                                        width:&brush.thumbnailWidth
                                                       height:&brush.thumbnailHeight];
        if (brush.thumbnailTextureId >= 0) {
            NSLog(@"[BrushImporter] Loaded thumbnail texture: %d (%dx%d)",
                  brush.thumbnailTextureId, brush.thumbnailWidth, brush.thumbnailHeight);
        }
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

    // Initialize default settings BEFORE parsing (so missing keys have sensible defaults)
    brush.settings.spacing = 0.1f;
    brush.settings.sizeJitter = 0.0f;
    brush.settings.opacityJitter = 0.0f;
    brush.settings.scatterAmount = 0.0f;
    brush.settings.rotationJitter = 0.0f;
    brush.settings.sizePressure = 0.8f;      // Default pressure sensitivity
    brush.settings.opacityPressure = 0.3f;   // Default pressure sensitivity
    brush.settings.grainScale = 1.0f;
    brush.settings.hardness = 0.5f;
    brush.settings.flow = 1.0f;
    brush.settings.shapeInverted = 0;

    // Parse settings from Brush.archive (overrides defaults for keys that exist)
    if ([fm fileExistsAtPath:brushArchiveURL.path]) {
        [self parseBrushArchive:brushArchiveURL settings:&brush.settings name:brush.name nameBufferSize:sizeof(brush.name)];
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

    // Check if image has alpha channel - Procreate brush shapes store the mask in alpha
    CGImageAlphaInfo alphaInfo = CGImageGetAlphaInfo(cgImage);
    BOOL hasAlpha = (alphaInfo != kCGImageAlphaNone && alphaInfo != kCGImageAlphaNoneSkipLast && alphaInfo != kCGImageAlphaNoneSkipFirst);

    // Prepare grayscale output buffer
    NSUInteger outputBytesPerRow = width;
    uint8_t* grayscaleData = (uint8_t*)calloc(height * outputBytesPerRow, sizeof(uint8_t));

    if (hasAlpha) {
        // Load as RGBA first to extract alpha channel
        NSUInteger rgbaBytesPerRow = width * 4;
        uint8_t* rgbaData = (uint8_t*)calloc(height * rgbaBytesPerRow, sizeof(uint8_t));

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(rgbaData, width, height,
                                                      8, rgbaBytesPerRow, colorSpace,
                                                      kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(colorSpace);

        if (!context) {
            free(rgbaData);
            free(grayscaleData);
            return -1;
        }

        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
        CGContextRelease(context);

        // Extract alpha channel as the grayscale mask
        // In Procreate brush shapes: alpha=255 means paint, alpha=0 means transparent
        for (NSUInteger i = 0; i < width * height; i++) {
            grayscaleData[i] = rgbaData[i * 4 + 3];  // Alpha is the 4th component (index 3)
        }

        free(rgbaData);
        NSLog(@"[BrushImporter] Loaded shape texture with ALPHA channel: %lux%lu", (unsigned long)width, (unsigned long)height);
    } else {
        // No alpha - use grayscale luminance directly
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceGray();
        CGContextRef context = CGBitmapContextCreate(grayscaleData, width, height,
                                                      8, outputBytesPerRow, colorSpace,
                                                      kCGImageAlphaNone);
        CGColorSpaceRelease(colorSpace);

        if (!context) {
            free(grayscaleData);
            return -1;
        }

        CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
        CGContextRelease(context);
        NSLog(@"[BrushImporter] Loaded shape texture as GRAYSCALE: %lux%lu", (unsigned long)width, (unsigned long)height);
    }

    // Load into Metal as R8 texture
    int32_t textureId = metal_stamp_load_texture_data(grayscaleData, (int)width, (int)height);

    free(grayscaleData);
    return textureId;
}

// Load thumbnail image with RGBA (preserves alpha channel for proper rendering)
+ (int32_t)loadThumbnailFromURL:(NSURL*)url width:(int32_t*)outWidth height:(int32_t*)outHeight {
    UIImage* image = [UIImage imageWithContentsOfFile:url.path];
    if (!image) return -1;

    CGImageRef cgImage = image.CGImage;
    NSUInteger width = CGImageGetWidth(cgImage);
    NSUInteger height = CGImageGetHeight(cgImage);

    // Store dimensions
    if (outWidth) *outWidth = (int32_t)width;
    if (outHeight) *outHeight = (int32_t)height;

    // Create RGBA context for proper alpha handling
    NSUInteger bytesPerPixel = 4;
    NSUInteger bytesPerRow = width * bytesPerPixel;
    uint8_t* rawData = (uint8_t*)calloc(height * bytesPerRow, sizeof(uint8_t));

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(rawData, width, height,
                                                  8, bytesPerRow, colorSpace,
                                                  kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colorSpace);

    if (!context) {
        free(rawData);
        return -1;
    }

    // Draw image
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
    CGContextRelease(context);

    // Load into Metal as RGBA texture
    int32_t textureId = metal_stamp_load_rgba_texture_data(rawData, (int)width, (int)height);

    free(rawData);
    return textureId;
}

// Helper to extract integer value from CFKeyedArchiverUID
// CFKeyedArchiverUID doesn't respond to intValue, so we parse its description
+ (NSInteger)extractUIDValue:(id)uidRef {
    if (!uidRef) return -1;

    // First try intValue/integerValue (for NSNumber)
    if ([uidRef respondsToSelector:@selector(integerValue)]) {
        return [uidRef integerValue];
    }

    // Parse CFKeyedArchiverUID description: "<CFKeyedArchiverUID ...>{value = 83}"
    NSString* desc = [uidRef description];
    NSRange range = [desc rangeOfString:@"value = "];
    if (range.location != NSNotFound) {
        NSString* valueStr = [desc substringFromIndex:range.location + range.length];
        valueStr = [valueStr stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"}"]];
        return [valueStr integerValue];
    }

    return -1;
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
        // name is a CFKeyedArchiverUID reference to another object in $objects
        id nameRef = brush[@"name"];
        NSInteger nameIndex = [self extractUIDValue:nameRef];
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

    // Shape inversion - determines how texture alpha is interpreted
    // shapeInverted=0: WHITE=opaque, BLACK=transparent (after CGBitmapContext fix)
    // shapeInverted=1: BLACK=opaque, WHITE=transparent (standard Procreate convention)
    if (brush[@"shapeInverted"]) {
        settings->shapeInverted = [brush[@"shapeInverted"] intValue];
    } else {
        settings->shapeInverted = 0;  // Default: not inverted
    }

    NSLog(@"[BrushImporter] Parsed settings: spacing=%.4f, sizeJitter=%.2f, opacityJitter=%.2f, scatter=%.2f, sizePressure=%.2f, opacityPressure=%.2f, grainScale=%.2f, flow=%.2f, shapeInverted=%d",
          settings->spacing, settings->sizeJitter, settings->opacityJitter, settings->scatterAmount,
          settings->sizePressure, settings->opacityPressure, settings->grainScale, settings->flow, settings->shapeInverted);

    return YES;
}

+ (NSArray<NSNumber*>*)importBrushSetFromURL:(NSURL*)url {
    NSMutableArray* brushIds = [NSMutableArray array];

    // Extract the brushset
    NSURL* libraryDir = [self brushLibraryDirectory];
    NSURL* extractedDir = [self extractBrushArchive:url toDirectory:libraryDir];

    if (!extractedDir) return @[];

    NSFileManager* fm = [NSFileManager defaultManager];

    // Parse brushset.plist to get the CORRECT brush order
    // Procreate stores the intended display order in this file
    NSURL* plistURL = [extractedDir URLByAppendingPathComponent:@"brushset.plist"];
    NSArray* brushOrder = nil;

    if ([fm fileExistsAtPath:plistURL.path]) {
        NSData* plistData = [NSData dataWithContentsOfURL:plistURL];
        if (plistData) {
            NSError* error;
            NSDictionary* plist = [NSPropertyListSerialization propertyListWithData:plistData
                                                                            options:NSPropertyListImmutable
                                                                             format:nil
                                                                              error:&error];
            if (plist && !error) {
                brushOrder = plist[@"brushes"];
                NSLog(@"[BrushImporter] Found brushset.plist with %lu brushes in order", (unsigned long)[brushOrder count]);
            }
        }
    }

    if (brushOrder && [brushOrder count] > 0) {
        // Import brushes in the ORDER specified by brushset.plist
        for (NSString* uuid in brushOrder) {
            NSURL* brushDir = [extractedDir URLByAppendingPathComponent:uuid];
            if ([fm fileExistsAtPath:brushDir.path]) {
                NSURL* brushArchive = [brushDir URLByAppendingPathComponent:@"Brush.archive"];
                if ([fm fileExistsAtPath:brushArchive.path]) {
                    int32_t brushId = [self importBrushFromExtractedDirectory:brushDir];
                    if (brushId > 0) {
                        [brushIds addObject:@(brushId)];
                    }
                }
            }
        }
    } else {
        // Fallback: Find all brush subdirectories and sort alphabetically
        NSLog(@"[BrushImporter] No brushset.plist found, using alphabetical order");
        NSArray* contents = [fm contentsOfDirectoryAtURL:extractedDir
                              includingPropertiesForKeys:@[NSURLIsDirectoryKey]
                                                 options:0
                                                   error:nil];

        contents = [contents sortedArrayUsingComparator:^NSComparisonResult(NSURL* a, NSURL* b) {
            return [[a lastPathComponent] compare:[b lastPathComponent]];
        }];

        for (NSURL* item in contents) {
            NSNumber* isDir;
            [item getResourceValue:&isDir forKey:NSURLIsDirectoryKey error:nil];

            if ([isDir boolValue]) {
                NSURL* brushArchive = [item URLByAppendingPathComponent:@"Brush.archive"];
                if ([fm fileExistsAtPath:brushArchive.path]) {
                    int32_t brushId = [self importBrushFromExtractedDirectory:item];
                    if (brushId > 0) {
                        [brushIds addObject:@(brushId)];
                    }
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
    brush.thumbnailTextureId = -1;
    brush.thumbnailWidth = 0;
    brush.thumbnailHeight = 0;

    // Set initial name from directory name (will be overwritten by Brush.archive if available)
    NSString* name = [dir lastPathComponent];
    strncpy(brush.name, [name UTF8String], sizeof(brush.name) - 1);

    // Look for thumbnail - check root level and Sub01 folder
    NSURL* thumbnailURL = [dir URLByAppendingPathComponent:@"QuickLook/Thumbnail.png"];
    if (![fm fileExistsAtPath:thumbnailURL.path]) {
        // Try Sub01 folder
        thumbnailURL = [dir URLByAppendingPathComponent:@"Sub01/QuickLook/Thumbnail.png"];
    }
    if ([fm fileExistsAtPath:thumbnailURL.path]) {
        strncpy(brush.thumbnailPath, [thumbnailURL.path UTF8String], sizeof(brush.thumbnailPath) - 1);
        // Load thumbnail as RGBA texture
        brush.thumbnailTextureId = [self loadThumbnailFromURL:thumbnailURL
                                                        width:&brush.thumbnailWidth
                                                       height:&brush.thumbnailHeight];
        if (brush.thumbnailTextureId >= 0) {
            NSLog(@"[BrushImporter] Loaded thumbnail texture: %d (%dx%d)",
                  brush.thumbnailTextureId, brush.thumbnailWidth, brush.thumbnailHeight);
        }
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

    // Parse settings from the SAME directory as the Shape.png
    // This ensures shapeInverted matches the actual Shape.png being used
    NSURL* archiveURL;
    if (brush.shapeTextureId >= 0) {
        // Use archive from same directory as the loaded shape
        NSURL* shapeDir = [shapeURL URLByDeletingLastPathComponent];
        archiveURL = [shapeDir URLByAppendingPathComponent:@"Brush.archive"];
        if (![fm fileExistsAtPath:archiveURL.path]) {
            // Fall back to root if no archive in shape's directory
            archiveURL = [dir URLByAppendingPathComponent:@"Brush.archive"];
        }
    } else {
        // No shape loaded, use root archive
        archiveURL = [dir URLByAppendingPathComponent:@"Brush.archive"];
    }

    if ([fm fileExistsAtPath:archiveURL.path]) {
        [self parseBrushArchive:archiveURL settings:&brush.settings name:brush.name nameBufferSize:sizeof(brush.name)];
        NSLog(@"[BrushImporter] Parsed settings from %@", archiveURL.path);
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

    // Check if brush is supported (must have a shape texture)
    // Brushes that use Procreate's bundled resources (bundledShapePath) don't have Shape.png
    if (brush->shapeTextureId < 0) {
        NSLog(@"[BrushImporter] UNSUPPORTED: Brush '%s' uses bundled Procreate resources (no Shape.png)", brush->name);
        return NO;  // Don't apply unsupported brushes - keep current brush active
    }

    // Set brush type: 1 = textured brush (has shape or grain texture)
    int brushType = (brush->shapeTextureId >= 0 || brush->grainTextureId >= 0) ? 1 : 0;
    metal_stamp_set_brush_type(brushType);

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
    } else {
        metal_stamp_set_brush_shape_texture(0);  // Use default procedural circle
    }
    if (brush->grainTextureId >= 0) {
        metal_stamp_set_brush_grain_texture(brush->grainTextureId);
    } else {
        metal_stamp_set_brush_grain_texture(0);  // No grain
    }

    // Set shape inversion mode
    metal_stamp_set_brush_shape_inverted(brush->settings.shapeInverted);

    NSLog(@"[BrushImporter] Applied brush '%s' (shapeInverted=%d)", brush->name, brush->settings.shapeInverted);
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

+ (NSArray<NSNumber*>*)loadBundledBrushZip:(NSString*)zipName {
    // Load a zip file containing .brushset and/or .brush files from the app bundle
    NSBundle* mainBundle = [NSBundle mainBundle];
    NSFileManager* fm = [NSFileManager defaultManager];
    NSMutableArray<NSNumber*>* allBrushIds = [NSMutableArray array];

    // Find the zip file in the bundle
    NSURL* zipURL = [mainBundle URLForResource:zipName withExtension:@"zip"];
    if (!zipURL) {
        // Try with the full name (in case it already has .zip)
        zipURL = [mainBundle URLForResource:zipName withExtension:nil];
    }
    if (!zipURL) {
        NSString* resourcePath = [mainBundle resourcePath];
        NSString* zipPath = [resourcePath stringByAppendingPathComponent:
                            [NSString stringWithFormat:@"%@.zip", zipName]];
        if ([fm fileExistsAtPath:zipPath]) {
            zipURL = [NSURL fileURLWithPath:zipPath];
        }
    }

    if (!zipURL) {
        NSLog(@"[BrushImporter] Bundled zip '%@' not found", zipName);
        return @[];
    }

    NSLog(@"[BrushImporter] Loading brushes from bundled zip: %@", zipURL.path);

    // Extract zip to temp directory using iOS-compatible extraction
    NSURL* tempDir = [[NSURL fileURLWithPath:NSTemporaryDirectory()]
                      URLByAppendingPathComponent:[[NSUUID UUID] UUIDString]];

    NSError* error;
    [fm createDirectoryAtURL:tempDir withIntermediateDirectories:YES attributes:nil error:&error];
    if (error) {
        NSLog(@"[BrushImporter] Failed to create temp directory: %@", error);
        return @[];
    }

    // Use our iOS-compatible ZIP extraction
    if (![self extractZipFile:zipURL toDirectory:tempDir]) {
        NSLog(@"[BrushImporter] Failed to extract zip file");
        [fm removeItemAtURL:tempDir error:nil];
        return @[];
    }

    NSArray<NSNumber*>* brushIds = [self loadBrushesFromDirectory:tempDir source:@"zip"];

    // Cleanup temp directory
    [fm removeItemAtURL:tempDir error:nil];

    return brushIds;
}

+ (NSArray<NSNumber*>*)loadBundledBrushFolder:(NSString*)folderName {
    // Load brushes from a pre-extracted folder in the app bundle
    NSBundle* mainBundle = [NSBundle mainBundle];
    NSFileManager* fm = [NSFileManager defaultManager];

    // Find the folder in the bundle
    NSURL* folderURL = [mainBundle URLForResource:folderName withExtension:nil];
    if (!folderURL) {
        NSString* resourcePath = [mainBundle resourcePath];
        NSString* folderPath = [resourcePath stringByAppendingPathComponent:folderName];
        if ([fm fileExistsAtPath:folderPath]) {
            folderURL = [NSURL fileURLWithPath:folderPath];
        }
    }

    if (!folderURL) {
        NSLog(@"[BrushImporter] Bundled folder '%@' not found", folderName);
        return @[];
    }

    NSLog(@"[BrushImporter] Loading brushes from bundled folder: %@", folderURL.path);
    return [self loadBrushesFromDirectory:folderURL source:@"folder"];
}

// Shared helper: scan directory for .brushset and .brush files and load them
+ (NSArray<NSNumber*>*)loadBrushesFromDirectory:(NSURL*)dirURL source:(NSString*)source {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSMutableArray<NSNumber*>* allBrushIds = [NSMutableArray array];

    NSDirectoryEnumerator* enumerator = [fm enumeratorAtURL:dirURL
                                 includingPropertiesForKeys:@[NSURLIsDirectoryKey]
                                                    options:NSDirectoryEnumerationSkipsHiddenFiles
                                               errorHandler:nil];

    NSMutableArray<NSURL*>* brushsetURLs = [NSMutableArray array];
    NSMutableArray<NSURL*>* brushURLs = [NSMutableArray array];

    for (NSURL* fileURL in enumerator) {
        NSString* filename = [fileURL lastPathComponent];
        if ([filename hasSuffix:@".brushset"]) {
            [brushsetURLs addObject:fileURL];
        } else if ([filename hasSuffix:@".brush"]) {
            [brushURLs addObject:fileURL];
        }
    }

    NSLog(@"[BrushImporter] Found %lu .brushset and %lu .brush files in %@",
          (unsigned long)brushsetURLs.count, (unsigned long)brushURLs.count, source);

    for (NSURL* brushsetURL in brushsetURLs) {
        NSLog(@"[BrushImporter] Loading brushset from %@: %@", source, [brushsetURL lastPathComponent]);
        NSArray<NSNumber*>* brushIds = [self importBrushSetFromURL:brushsetURL];
        [allBrushIds addObjectsFromArray:brushIds];
    }

    for (NSURL* brushURL in brushURLs) {
        NSLog(@"[BrushImporter] Loading brush from %@: %@", source, [brushURL lastPathComponent]);
        int32_t brushId = [self importBrushFromURL:brushURL];
        if (brushId >= 0) {
            [allBrushIds addObject:@(brushId)];
        }
    }

    NSLog(@"[BrushImporter] Loaded %lu total brushes from %@", (unsigned long)allBrushIds.count, source);
    return allBrushIds;
}

@end
