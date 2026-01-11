// Metal Stamp Renderer - Objective-C++ Implementation
// Implements GPU stamp rendering using Metal for iOS/macOS

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include "metal_renderer.h"
#include "undo_tree.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef __APPLE__
#include <os/log.h>
#define METAL_LOG(fmt, ...) os_log_info(OS_LOG_DEFAULT, "[MetalRenderer] " fmt, ##__VA_ARGS__)
#else
#define METAL_LOG(fmt, ...) printf("[MetalRenderer] " fmt "\n", ##__VA_ARGS__)
#endif

// SDL includes (for window access)
#include <SDL3/SDL.h>

// =============================================================================
// Deterministic Pseudo-Random Number Generator (for reproducible jitter/scatter)
// Uses a simple hash function - same seed + counter always produces same result
// =============================================================================

// Deterministic hash function based on seed and counter
// Returns a float in range [0, 1)
inline float deterministic_random(uint32_t seed, uint32_t counter) {
    // Mix seed and counter using a simple hash (based on xxhash/murmur principles)
    uint32_t h = seed;
    h ^= counter;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    // Convert to float [0, 1)
    return (float)(h & 0x00FFFFFF) / (float)0x01000000;
}

// =============================================================================
// MSL Data Structures (must match stamp_shaders.metal)
// NOTE: These are at global scope for use by Objective-C code
// =============================================================================

struct MSLPoint {
    simd_float2 position;  // Metal NDC (-1 to 1)
    float size;            // Point size in pixels
    simd_float4 color;     // RGBA
};

struct MSLStrokeUniforms {
    simd_float2 viewportSize;
    float hardness;
    float opacity;
    float flow;
    float grainScale;
    simd_float2 grainOffset;  // For moving grain mode
    int useShapeTexture;      // 0 = procedural, 1 = texture
    int useGrainTexture;      // 0 = no grain, 1 = use grain
    int shapeInverted;        // 0 = WHITE=opaque, 1 = BLACK=opaque
};

// =============================================================================
// MetalStampRendererImpl - Objective-C++ Implementation
// Must be at global scope (Objective-C requirement)
// =============================================================================

@interface MetalStampRendererImpl : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLRenderPipelineState> stampPipeline;        // Round brush
@property (nonatomic, strong) id<MTLRenderPipelineState> crayonPipeline;       // Crayon brush
@property (nonatomic, strong) id<MTLRenderPipelineState> watercolorPipeline;   // Watercolor brush
@property (nonatomic, strong) id<MTLRenderPipelineState> markerPipeline;       // Marker brush
@property (nonatomic, strong) id<MTLRenderPipelineState> stampTexturePipeline; // With shape texture
@property (nonatomic, strong) id<MTLRenderPipelineState> clearPipeline;
@property (nonatomic, strong) id<MTLRenderPipelineState> uiRectPipeline;      // UI rectangle drawing
@property (nonatomic, strong) id<MTLRenderPipelineState> uiTexturedRectPipeline; // UI textured rectangle drawing
@property (nonatomic, strong) id<MTLRenderPipelineState> canvasBlitPipeline; // Canvas blit with transform
@property (nonatomic, assign) int currentBrushType;  // 0=Round, 1=Crayon, etc.
@property (nonatomic, strong) id<MTLTexture> canvasTexture;
@property (nonatomic, strong) id<MTLBuffer> pointBuffer;
@property (nonatomic, strong) id<MTLBuffer> uniformBuffer;
@property (nonatomic, strong) id<MTLSamplerState> textureSampler;
@property (nonatomic, assign) CAMetalLayer* metalLayer;

// Brush textures
@property (nonatomic, strong) id<MTLTexture> currentShapeTexture;
@property (nonatomic, strong) id<MTLTexture> currentGrainTexture;
@property (nonatomic, assign) int shapeInverted;  // 0 = WHITE=opaque, 1 = BLACK=opaque
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, id<MTLTexture>>* loadedTextures;
@property (nonatomic, assign) int32_t nextTextureId;

@property (nonatomic, assign) int width;           // Point size (for input)
@property (nonatomic, assign) int height;          // Point size (for input)
@property (nonatomic, assign) int drawableWidth;   // Pixel size (for screen rendering)
@property (nonatomic, assign) int drawableHeight;  // Pixel size (for screen rendering)
@property (nonatomic, assign) int canvasWidth;     // Canvas texture resolution (independent of screen)
@property (nonatomic, assign) int canvasHeight;    // Canvas texture resolution (independent of screen)
@property (nonatomic, assign) BOOL isDrawing;
@property (nonatomic, assign) simd_float2 lastPoint;
@property (nonatomic, assign) simd_float2 grainOffset;  // Accumulated grain offset for moving mode
@property (nonatomic, assign) int pointCount;
@property (nonatomic, assign) int pointsRendered;

// For auto-flush: brush settings stored at stroke start
@property (nonatomic, assign) float strokeHardness;
@property (nonatomic, assign) float strokeOpacity;
@property (nonatomic, assign) float strokeFlow;
@property (nonatomic, assign) float strokeGrainScale;
@property (nonatomic, assign) BOOL strokeUseShape;
@property (nonatomic, assign) BOOL strokeUseGrain;

// Deterministic random state for reproducible jitter/scatter
@property (nonatomic, assign) uint32_t strokeRandomSeed;   // Set at stroke begin
@property (nonatomic, assign) uint32_t strokeRandomCounter; // Increments for each "random" value

// Track which points have been rendered to avoid double-rendering
// This is CRITICAL for matching original vs replay appearance
@property (nonatomic, assign) size_t renderedPointCount;   // Points already rendered to canvas

// Frame texture cache for instant frame switching (animation)
@property (nonatomic, strong) NSMutableArray<id<MTLTexture>>* frameTextureCache;
@property (nonatomic, strong) NSMutableArray<NSNumber*>* frameCacheValid;  // Track which frames have content
@property (nonatomic, assign) int activeFrameIndex;
@property (nonatomic, assign) int maxFrames;  // Default 12 for Looom-style wheel

- (BOOL)initWithWindow:(SDL_Window*)window width:(int)w height:(int)h;
- (void)cleanup;
- (BOOL)createPipelines;
- (BOOL)createCanvasTexture;
- (BOOL)createTextureSampler;
- (void)resizeWithWidth:(int)w height:(int)h;

// Coordinate conversion
- (simd_float2)screenToNDC:(float)x y:(float)y;

// Point interpolation with jitter/scatter
- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing
                scatter:(float)scatter sizeJitter:(float)sizeJitter opacityJitter:(float)opacityJitter;

// Texture management
- (int32_t)loadTextureFromFile:(NSString*)path;
- (int32_t)loadTextureFromData:(const uint8_t*)data width:(int)width height:(int)height;
- (int32_t)loadTextureFromRGBAData:(const uint8_t*)data width:(int)width height:(int)height;
- (id<MTLTexture>)getTextureById:(int32_t)textureId;
- (void)unloadTexture:(int32_t)textureId;

// Rendering
- (simd_float4)backgroundColor;
- (void)clearCanvasWithColor:(simd_float4)color;
- (void)renderPointsWithHardness:(float)hardness opacity:(float)opacity
                            flow:(float)flow grainScale:(float)grainScale
                  useShapeTexture:(BOOL)useShape useGrainTexture:(BOOL)useGrain;
- (void)commitStrokeToCanvas;

// UI Drawing
- (void)queueUIRect:(float)x y:(float)y width:(float)w height:(float)h
              color:(simd_float4)color cornerRadius:(float)radius;
- (void)queueUITexturedRect:(float)x y:(float)y width:(float)w height:(float)h
                  textureId:(int32_t)textureId tint:(simd_float4)tint;
- (void)drawQueuedUIRectsToTexture:(id<MTLTexture>)texture;
- (void)clearUIQueue;

// Canvas Transform
- (void)setCanvasTransformPanX:(float)panX panY:(float)panY
                         scale:(float)scale rotation:(float)rotation
                        pivotX:(float)pivotX pivotY:(float)pivotY;
- (void)drawCanvasToTexture:(id<MTLTexture>)targetTexture;
- (BOOL)hasCanvasTransform;

// Canvas Snapshots
- (NSData*)captureCanvasSnapshot;
- (BOOL)restoreCanvasSnapshot:(NSData*)pixels width:(int)width height:(int)height;

// Delta Snapshots (capture only changed region)
- (NSData*)captureDeltaSnapshotX:(int)x y:(int)y width:(int)w height:(int)h;
- (BOOL)restoreDeltaSnapshot:(NSData*)pixels atX:(int)x y:(int)y width:(int)w height:(int)h;

// Frame texture cache (for instant animation frame switching)
- (BOOL)initFrameCache:(int)maxFrames;
- (BOOL)cacheCurrentFrameToGPU:(int)frameIndex;  // Copy canvas to cached texture
- (BOOL)switchToCachedFrame:(int)frameIndex;      // Instant switch (no CPU->GPU transfer)
- (BOOL)isFrameCached:(int)frameIndex;
- (void)clearFrameCache;

@end

// UI Rect parameters (matches shader struct)
struct UIRectParams {
    simd_float4 rect;       // x, y, width, height in NDC
    simd_float4 color;
    float cornerRadius;
    float _padding[3];      // Align to 16 bytes
};

// UI Textured Rect parameters (matches shader struct)
struct UITexturedRectParams {
    simd_float4 rect;       // x, y, width, height in NDC
    simd_float4 tint;       // RGBA tint color (multiplied with texture)
    int32_t textureId;      // Reference to texture
    float _padding[3];
};

// Canvas transform uniforms (matches shader struct)
struct CanvasTransformUniforms {
    simd_float2 pan;           // Pan offset in screen pixels
    float scale;               // Zoom level (1.0 = 100%)
    float rotation;            // Rotation in radians
    simd_float2 pivot;         // Transform pivot in pixels
    simd_float2 viewportSize;  // Screen/viewport size in pixels
    simd_float2 canvasSize;    // Canvas texture size in pixels (may differ from viewport)
};

@implementation MetalStampRendererImpl {
    std::vector<MSLPoint> _points;
    simd_float4 _backgroundColor;
    std::vector<UIRectParams> _uiRects;  // UI rects to draw this frame
    std::vector<UITexturedRectParams> _uiTexturedRects;  // Textured UI rects to draw this frame
    CanvasTransformUniforms _canvasTransform;  // Canvas pan/zoom/rotate
}

- (BOOL)initWithWindow:(SDL_Window*)window width:(int)w height:(int)h {
    self.width = w;
    self.height = h;
    self.isDrawing = NO;
    self.pointCount = 0;
    self.pointsRendered = 0;
    self.renderedPointCount = 0;
    self.grainOffset = simd_make_float2(0.0f, 0.0f);
    // Texture ID convention: 0 = "no texture", valid IDs start at 1
    // (brush_importer uses -1 for "not loaded", so both systems are compatible)
    self.nextTextureId = 1;
    self.currentBrushType = 1;  // Default to Crayon brush!
    self.loadedTextures = [NSMutableDictionary dictionary];
    self.currentShapeTexture = nil;
    self.currentGrainTexture = nil;
    _backgroundColor = simd_make_float4(
        metal_stamp::DEFAULT_PAPER_COLOR_R,
        metal_stamp::DEFAULT_PAPER_COLOR_G,
        metal_stamp::DEFAULT_PAPER_COLOR_B,
        metal_stamp::DEFAULT_PAPER_COLOR_A);

    // Initialize canvas transform to identity
    _canvasTransform.pan = simd_make_float2(0.0f, 0.0f);
    _canvasTransform.scale = 1.0f;
    _canvasTransform.rotation = 0.0f;
    _canvasTransform.pivot = simd_make_float2(0.0f, 0.0f);  // Will be set from drawable size
    _canvasTransform.viewportSize = simd_make_float2(w, h);

    // Create Metal device
    self.device = MTLCreateSystemDefaultDevice();
    if (!self.device) {
        METAL_LOG("Failed to create Metal device");
        return NO;
    }
    METAL_LOG("Created Metal device: %s", [[self.device name] UTF8String]);

    // Create command queue
    self.commandQueue = [self.device newCommandQueue];
    if (!self.commandQueue) {
        METAL_LOG("Failed to create command queue");
        return NO;
    }

    // Get the Metal layer from SDL window using SDL3 Metal API
    // SDL_Metal_GetLayer returns the CAMetalLayer for Metal-backed windows
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    if (!metalView) {
        METAL_LOG("Failed to create Metal view from SDL window: %s", SDL_GetError());
        std::cerr << "[MetalRenderer] SDL_Metal_CreateView failed: " << SDL_GetError() << std::endl;
        return NO;
    }

    self.metalLayer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
    if (!self.metalLayer) {
        METAL_LOG("Failed to get CAMetalLayer from Metal view");
        std::cerr << "[MetalRenderer] SDL_Metal_GetLayer returned NULL" << std::endl;
        return NO;
    }
    METAL_LOG("Got Metal layer from SDL window");

    self.metalLayer.device = self.device;
    self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalLayer.framebufferOnly = NO;  // Allow reading back

    // Get actual drawable size (may differ from point size on Retina displays)
    CGSize drawableSize = self.metalLayer.drawableSize;
    self.drawableWidth = (int)drawableSize.width;
    self.drawableHeight = (int)drawableSize.height;

    // Set canvas resolution independent of screen - default to 4K (3840x2160)
    // This allows high-res drawing regardless of screen size
    self.canvasWidth = 3840;
    self.canvasHeight = 2160;

    // Update canvas transform with viewport and canvas sizes
    // Note: Actual pan/scale/rotation are set by drawing_mobile_ios.mm via metal_stamp_set_canvas_transform()
    _canvasTransform.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);
    _canvasTransform.canvasSize = simd_make_float2(self.canvasWidth, self.canvasHeight);
    _canvasTransform.scale = 1.0f;
    _canvasTransform.pan = simd_make_float2(0.0f, 0.0f);

    METAL_LOG("Point size: %dx%d, Drawable size: %dx%d, Canvas: %dx%d",
              w, h, self.drawableWidth, self.drawableHeight, self.canvasWidth, self.canvasHeight);
    NSLog(@"[Metal] Point size: %dx%d, Drawable size: %dx%d, Canvas: %dx%d",
          w, h, self.drawableWidth, self.drawableHeight, self.canvasWidth, self.canvasHeight);

    // Create render pipelines
    if (![self createPipelines]) {
        return NO;
    }

    // Create canvas texture
    if (![self createCanvasTexture]) {
        return NO;
    }

    // Create texture sampler for brush textures
    if (![self createTextureSampler]) {
        return NO;
    }

    // Create point buffer (will be resized as needed)
    self.pointBuffer = [self.device newBufferWithLength:sizeof(MSLPoint) * metal_stamp::MAX_POINTS_PER_STROKE
                                                options:MTLResourceStorageModeShared];

    // Create uniform buffer
    self.uniformBuffer = [self.device newBufferWithLength:sizeof(MSLStrokeUniforms)
                                                  options:MTLResourceStorageModeShared];

    _points.reserve(metal_stamp::MAX_POINTS_PER_STROKE);

    METAL_LOG("Initialized successfully (%dx%d)", w, h);
    return YES;
}

- (void)cleanup {
    self.pointBuffer = nil;
    self.uniformBuffer = nil;
    self.canvasTexture = nil;
    self.stampPipeline = nil;
    self.clearPipeline = nil;
    self.commandQueue = nil;
    self.device = nil;
    _points.clear();
    METAL_LOG("Cleaned up");
}

- (BOOL)createPipelines {
    NSError* error = nil;

    // Load shader library from default library (compiled from .metal files)
    id<MTLLibrary> library = [self.device newDefaultLibrary];
    if (library) {
        // Check if it actually has our functions
        NSArray* funcNames = [library functionNames];
        METAL_LOG("Default library has %lu functions: %s",
                  (unsigned long)[funcNames count],
                  [[funcNames description] UTF8String]);
        if (![funcNames containsObject:@"stamp_vertex"]) {
            METAL_LOG("Default library missing stamp_vertex, will compile from source");
            library = nil;  // Force source compilation
        }
    } else {
        METAL_LOG("No default library found");
    }

    if (!library) {
        // Try loading from explicit path (for iOS JIT)
        NSString* shaderPath = [[NSBundle mainBundle] pathForResource:@"stamp_shaders"
                                                               ofType:@"metallib"];
        METAL_LOG("Shader path: %s", shaderPath ? [shaderPath UTF8String] : "nil");
        if (shaderPath) {
            library = [self.device newLibraryWithFile:shaderPath error:&error];
            if (library) {
                METAL_LOG("Loaded shaders from metallib file");
            } else {
                METAL_LOG("Failed to load metallib: %s", [[error localizedDescription] UTF8String]);
            }
        }

        if (!library) {
            // Compile shaders from source as fallback
            NSString* shaderSource = @R"(
                #include <metal_stdlib>
                using namespace metal;

                struct Point {
                    float2 position;
                    float size;
                    float4 color;
                };

                struct PointVertexOutput {
                    float4 position [[position]];
                    float pointSize [[point_size]];
                    float4 color;
                };

                struct StrokeUniforms {
                    float2 viewportSize;
                    float hardness;
                    float opacity;
                };

                vertex PointVertexOutput stamp_vertex(
                    constant Point* points [[buffer(0)]],
                    constant StrokeUniforms& uniforms [[buffer(1)]],
                    uint vid [[vertex_id]]
                ) {
                    PointVertexOutput out;
                    out.position = float4(points[vid].position, 0.0, 1.0);
                    out.pointSize = points[vid].size;
                    out.color = points[vid].color;
                    return out;
                }

                fragment half4 stamp_fragment(
                    PointVertexOutput in [[stage_in]],
                    float2 pointCoord [[point_coord]],
                    constant StrokeUniforms& uniforms [[buffer(1)]]
                ) {
                    float2 centered = pointCoord - float2(0.5);
                    float dist = length(centered) * 2.0;

                    float inner_radius = uniforms.hardness * 0.9;
                    float outer_radius = 1.0;
                    float alpha = 1.0 - smoothstep(inner_radius, outer_radius, dist);

                    if (alpha <= 0.0) {
                        discard_fragment();
                    }

                    float4 out_color = in.color;
                    out_color.a *= alpha * uniforms.opacity;
                    return half4(out_color);
                }

                vertex float4 clear_vertex(uint vid [[vertex_id]], constant float4& color [[buffer(0)]]) {
                    float2 positions[4] = { float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1) };
                    return float4(positions[vid], 0.0, 1.0);
                }

                fragment half4 clear_fragment(constant float4& color [[buffer(0)]]) {
                    return half4(color);
                }

                // UI Rectangle shader - takes rect bounds in NDC and color
                struct UIRectParams {
                    float4 rect;   // x, y, width, height in NDC
                    float4 color;
                    float cornerRadius;  // In NDC units
                };

                struct UIVertexOut {
                    float4 position [[position]];
                    float2 uv;
                };

                vertex UIVertexOut ui_rect_vertex(uint vid [[vertex_id]], constant UIRectParams& params [[buffer(0)]]) {
                    // Quad corners: 0=BL, 1=BR, 2=TL, 3=TR
                    float2 corners[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };
                    float2 uv = corners[vid];

                    float2 pos = params.rect.xy + uv * params.rect.zw;

                    UIVertexOut out;
                    out.position = float4(pos, 0.0, 1.0);
                    out.uv = uv;
                    return out;
                }

                fragment half4 ui_rect_fragment(UIVertexOut in [[stage_in]], constant UIRectParams& params [[buffer(0)]]) {
                    // Simple rounded rectangle SDF
                    float2 size = params.rect.zw;
                    float2 center = float2(0.5, 0.5);
                    float2 p = in.uv - center;

                    // Aspect-correct radius
                    float r = params.cornerRadius / min(size.x, size.y);
                    float2 q = abs(p) - (float2(0.5) - r);
                    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;

                    // Antialiased edge
                    float aa = fwidth(d) * 1.5;
                    float alpha = 1.0 - smoothstep(-aa, aa, d);

                    return half4(half3(params.color.rgb), half(params.color.a * alpha));
                }

                // UI Textured Rectangle shader - samples texture and multiplies by tint
                struct UITexturedRectParams {
                    float4 rect;       // x, y, width, height in NDC
                    float4 tint;       // RGBA tint color (multiplied with texture)
                    int textureId;     // Reference to texture (not used in shader)
                };

                vertex UIVertexOut ui_textured_rect_vertex(uint vid [[vertex_id]], constant UITexturedRectParams& params [[buffer(0)]]) {
                    float2 corners[4] = { float2(0,0), float2(1,0), float2(0,1), float2(1,1) };
                    float2 uv = corners[vid];
                    float2 pos = params.rect.xy + uv * params.rect.zw;
                    UIVertexOut out;
                    out.position = float4(pos, 0.0, 1.0);
                    out.uv = uv;
                    return out;
                }

                fragment half4 ui_textured_rect_fragment(UIVertexOut in [[stage_in]],
                                                         constant UITexturedRectParams& params [[buffer(0)]],
                                                         texture2d<float> tex [[texture(0)]],
                                                         sampler s [[sampler(0)]]) {
                    // Flip Y for correct texture orientation
                    float2 texCoord = float2(in.uv.x, 1.0 - in.uv.y);
                    float4 texColor = tex.sample(s, texCoord);
                    float4 result = texColor * params.tint;
                    return half4(result);
                }
            )";

            library = [self.device newLibraryWithSource:shaderSource
                                                options:nil
                                                  error:&error];
            if (!library) {
                METAL_LOG("Failed to compile shaders: %s", [[error localizedDescription] UTF8String]);
                return NO;
            }
            METAL_LOG("Compiled shaders from source");
        }
    }

    // Create stamp pipeline (point rendering)
    METAL_LOG("Looking for shader functions in library...");
    id<MTLFunction> vertexFunc = [library newFunctionWithName:@"stamp_vertex"];
    id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"stamp_fragment"];

    if (!vertexFunc || !fragmentFunc) {
        NSArray* funcNames = [library functionNames];
        METAL_LOG("Failed to find shader functions. Library has: %s",
                  [[funcNames description] UTF8String]);
        METAL_LOG("  vertexFunc=%p fragmentFunc=%p", vertexFunc, fragmentFunc);
        return NO;
    }

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFunc;
    pipelineDesc.fragmentFunction = fragmentFunc;
    pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    // Enable alpha blending (Porter-Duff "over" for RGB, preserve alpha for canvas)
    // RGB: src * src.a + dst * (1 - src.a) - standard alpha blend for color
    // Alpha: keep destination alpha at 1.0 - prevents canvas darkening when blitted to gray background
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;  // Don't modify dest alpha
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;  // Keep dest alpha = 1.0

    self.stampPipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    if (!self.stampPipeline) {
        METAL_LOG("Failed to create stamp pipeline: %s", [[error localizedDescription] UTF8String]);
        return NO;
    }

    // Create crayon pipeline
    id<MTLFunction> crayonFragmentFunc = [library newFunctionWithName:@"stamp_fragment_crayon"];
    if (crayonFragmentFunc) {
        pipelineDesc.fragmentFunction = crayonFragmentFunc;
        self.crayonPipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!self.crayonPipeline) {
            METAL_LOG("Failed to create crayon pipeline: %s", [[error localizedDescription] UTF8String]);
            // Non-fatal, fall back to stamp pipeline
        } else {
            METAL_LOG("Created crayon pipeline");
        }
    } else {
        METAL_LOG("Crayon shader not found in library, using fallback");
    }

    // Create watercolor pipeline
    id<MTLFunction> watercolorFragmentFunc = [library newFunctionWithName:@"stamp_fragment_watercolor"];
    if (watercolorFragmentFunc) {
        pipelineDesc.fragmentFunction = watercolorFragmentFunc;
        self.watercolorPipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!self.watercolorPipeline) {
            METAL_LOG("Failed to create watercolor pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created watercolor pipeline");
        }
    } else {
        METAL_LOG("Watercolor shader not found in library, using fallback");
    }

    // Create marker pipeline
    id<MTLFunction> markerFragmentFunc = [library newFunctionWithName:@"stamp_fragment_marker"];
    if (markerFragmentFunc) {
        pipelineDesc.fragmentFunction = markerFragmentFunc;
        self.markerPipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!self.markerPipeline) {
            METAL_LOG("Failed to create marker pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created marker pipeline");
        }
    } else {
        METAL_LOG("Marker shader not found in library, using fallback");
    }

    // Create textured stamp pipeline (for imported brush shape textures)
    id<MTLFunction> texturedFragmentFunc = [library newFunctionWithName:@"stamp_textured_fragment"];
    if (texturedFragmentFunc) {
        pipelineDesc.fragmentFunction = texturedFragmentFunc;
        self.stampTexturePipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!self.stampTexturePipeline) {
            METAL_LOG("Failed to create textured stamp pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created textured stamp pipeline for brush shapes");
        }
    } else {
        METAL_LOG("Textured stamp shader not found in library");
    }

    // Create UI rect pipeline
    id<MTLFunction> uiRectVertexFunc = [library newFunctionWithName:@"ui_rect_vertex"];
    id<MTLFunction> uiRectFragmentFunc = [library newFunctionWithName:@"ui_rect_fragment"];
    if (uiRectVertexFunc && uiRectFragmentFunc) {
        MTLRenderPipelineDescriptor* uiPipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiPipelineDesc.vertexFunction = uiRectVertexFunc;
        uiPipelineDesc.fragmentFunction = uiRectFragmentFunc;
        uiPipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        // Enable alpha blending for UI
        uiPipelineDesc.colorAttachments[0].blendingEnabled = YES;
        uiPipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        uiPipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        uiPipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        uiPipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        uiPipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        uiPipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        self.uiRectPipeline = [self.device newRenderPipelineStateWithDescriptor:uiPipelineDesc error:&error];
        if (!self.uiRectPipeline) {
            METAL_LOG("Failed to create UI rect pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created UI rect pipeline");
        }
    } else {
        METAL_LOG("UI rect shaders not found in library (vertex=%p fragment=%p)", uiRectVertexFunc, uiRectFragmentFunc);
    }

    // Create UI textured rect pipeline
    id<MTLFunction> uiTexturedRectVertexFunc = [library newFunctionWithName:@"ui_textured_rect_vertex"];
    id<MTLFunction> uiTexturedRectFragmentFunc = [library newFunctionWithName:@"ui_textured_rect_fragment"];
    NSLog(@"[Metal] Textured rect shaders: vertex=%p fragment=%p", uiTexturedRectVertexFunc, uiTexturedRectFragmentFunc);
    if (uiTexturedRectVertexFunc && uiTexturedRectFragmentFunc) {
        MTLRenderPipelineDescriptor* uiTexPipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiTexPipelineDesc.vertexFunction = uiTexturedRectVertexFunc;
        uiTexPipelineDesc.fragmentFunction = uiTexturedRectFragmentFunc;
        uiTexPipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        // Enable alpha blending for textured UI
        uiTexPipelineDesc.colorAttachments[0].blendingEnabled = YES;
        uiTexPipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        uiTexPipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        uiTexPipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        uiTexPipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        uiTexPipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        uiTexPipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        self.uiTexturedRectPipeline = [self.device newRenderPipelineStateWithDescriptor:uiTexPipelineDesc error:&error];
        if (!self.uiTexturedRectPipeline) {
            METAL_LOG("Failed to create UI textured rect pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created UI textured rect pipeline");
        }
    } else {
        METAL_LOG("UI textured rect shaders not found in library (vertex=%p fragment=%p)", uiTexturedRectVertexFunc, uiTexturedRectFragmentFunc);
    }

    // Create canvas blit pipeline (for transformed canvas rendering)
    id<MTLFunction> canvasBlitVertexFunc = [library newFunctionWithName:@"canvas_blit_vertex"];
    id<MTLFunction> canvasBlitFragmentFunc = [library newFunctionWithName:@"canvas_blit_fragment"];
    if (canvasBlitVertexFunc && canvasBlitFragmentFunc) {
        MTLRenderPipelineDescriptor* canvasBlitPipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        canvasBlitPipelineDesc.vertexFunction = canvasBlitVertexFunc;
        canvasBlitPipelineDesc.fragmentFunction = canvasBlitFragmentFunc;
        canvasBlitPipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        // No blending needed - canvas covers entire screen
        canvasBlitPipelineDesc.colorAttachments[0].blendingEnabled = NO;

        self.canvasBlitPipeline = [self.device newRenderPipelineStateWithDescriptor:canvasBlitPipelineDesc error:&error];
        if (!self.canvasBlitPipeline) {
            METAL_LOG("Failed to create canvas blit pipeline: %s", [[error localizedDescription] UTF8String]);
        } else {
            METAL_LOG("Created canvas blit pipeline");
        }
    } else {
        METAL_LOG("Canvas blit shaders not found in library (vertex=%p fragment=%p)", canvasBlitVertexFunc, canvasBlitFragmentFunc);
    }

    METAL_LOG("Created render pipelines");
    return YES;
}

- (BOOL)createCanvasTexture {
    // Use canvas resolution (independent of screen size) for high-res drawing
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:self.canvasWidth
                                    height:self.canvasHeight
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;  // Shared allows CPU read for snapshots

    self.canvasTexture = [self.device newTextureWithDescriptor:desc];
    if (!self.canvasTexture) {
        METAL_LOG("Failed to create canvas texture");
        return NO;
    }

    // Clear to background color
    [self clearCanvasWithColor:_backgroundColor];

    METAL_LOG("Created canvas texture (%dx%d)", self.canvasWidth, self.canvasHeight);
    return YES;
}

- (BOOL)createTextureSampler {
    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;  // For tiling grain textures
    samplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;

    self.textureSampler = [self.device newSamplerStateWithDescriptor:samplerDesc];
    if (!self.textureSampler) {
        METAL_LOG("Failed to create texture sampler");
        return NO;
    }

    METAL_LOG("Created texture sampler");
    return YES;
}

- (int32_t)loadTextureFromFile:(NSString*)path {
    // Load image from file
    UIImage* image = [UIImage imageWithContentsOfFile:path];
    if (!image) {
        // Try loading from bundle
        image = [UIImage imageNamed:path];
    }
    if (!image) {
        METAL_LOG("Failed to load image from: %s", [path UTF8String]);
        return 0;
    }

    CGImageRef cgImage = image.CGImage;
    size_t width = CGImageGetWidth(cgImage);
    size_t height = CGImageGetHeight(cgImage);

    // Create texture
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [self.device newTextureWithDescriptor:desc];
    if (!texture) {
        METAL_LOG("Failed to create texture for: %s", [path UTF8String]);
        return 0;
    }

    // Get image data
    size_t bytesPerRow = 4 * width;
    uint8_t* imageData = (uint8_t*)malloc(bytesPerRow * height);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        imageData, width, height, 8, bytesPerRow, colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);

    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    // Upload to texture
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:imageData bytesPerRow:bytesPerRow];

    free(imageData);

    // Store and return ID
    int32_t textureId = self.nextTextureId++;
    self.loadedTextures[@(textureId)] = texture;

    METAL_LOG("Loaded texture %d from: %s (%zux%zu)", textureId, [path UTF8String], width, height);
    return textureId;
}

- (int32_t)loadTextureFromData:(const uint8_t*)data width:(int)width height:(int)height {
    // Create texture for single-channel grayscale data
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [self.device newTextureWithDescriptor:desc];
    if (!texture) {
        METAL_LOG("Failed to create texture from data (%dx%d)", width, height);
        return 0;
    }

    // Upload data
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:width];

    // Store and return ID
    int32_t textureId = self.nextTextureId++;
    self.loadedTextures[@(textureId)] = texture;

    METAL_LOG("Loaded texture %d from data (%dx%d)", textureId, width, height);
    return textureId;
}

- (int32_t)loadTextureFromRGBAData:(const uint8_t*)data width:(int)width height:(int)height {
    // Create texture for RGBA data (4 bytes per pixel)
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;

    id<MTLTexture> texture = [self.device newTextureWithDescriptor:desc];
    if (!texture) {
        METAL_LOG("Failed to create RGBA texture from data (%dx%d)", width, height);
        return 0;
    }

    // Upload data (4 bytes per pixel)
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:width * 4];

    // Store and return ID
    int32_t textureId = self.nextTextureId++;
    self.loadedTextures[@(textureId)] = texture;

    METAL_LOG("Loaded RGBA texture %d from data (%dx%d)", textureId, width, height);
    return textureId;
}

- (id<MTLTexture>)getTextureById:(int32_t)textureId {
    // 0 = "no texture" by convention (valid IDs start at 1)
    if (textureId == 0) return nil;
    return self.loadedTextures[@(textureId)];
}

- (void)unloadTexture:(int32_t)textureId {
    if (textureId == 0) return;
    [self.loadedTextures removeObjectForKey:@(textureId)];
    METAL_LOG("Unloaded texture %d", textureId);
}

- (void)resizeWithWidth:(int)w height:(int)h {
    if (w == self.width && h == self.height) return;

    self.width = w;
    self.height = h;

    // Update drawable size from layer
    CGSize drawableSize = self.metalLayer.drawableSize;
    self.drawableWidth = (int)drawableSize.width;
    self.drawableHeight = (int)drawableSize.height;

    // Recreate canvas texture
    [self createCanvasTexture];

    METAL_LOG("Resized to %dx%d (drawable: %dx%d)", w, h, self.drawableWidth, self.drawableHeight);
}

- (simd_float2)screenToNDC:(float)x y:(float)y {
    // NOTE: Input (x, y) is ALREADY in canvas pixel coordinates!
    // The caller (drawing_mobile_ios.mm screenToCanvas) has already applied
    // the inverse view transform to convert screen → canvas coordinates.
    //
    // We just need to convert canvas pixels to NDC for the stroke shader.

    // Convert canvas pixels to NDC (-1 to 1)
    float ndcX = (x / self.canvasWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / self.canvasHeight) * 2.0f;  // Flip Y

    return simd_make_float2(ndcX, ndcY);
}

- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing
                scatter:(float)scatter sizeJitter:(float)sizeJitter opacityJitter:(float)opacityJitter {
    // Calculate distance in CANVAS space (not screen space!)
    // NDC range is -1 to 1, so multiply by canvasSize/2 to get canvas pixels
    float dx = (to.x - from.x) * self.canvasWidth * 0.5f;
    float dy = (to.y - from.y) * self.canvasHeight * 0.5f;
    float distance = sqrtf(dx * dx + dy * dy);

    // Calculate number of points needed
    float stepSize = size * spacing;
    if (stepSize < 1.0f) stepSize = 1.0f;

    int numPoints = (int)(distance / stepSize);
    if (numPoints < 1) numPoints = 1;

    // Direction perpendicular to stroke (for scatter)
    float strokeLen = sqrtf(dx * dx + dy * dy);
    float perpX = (strokeLen > 0.001f) ? -dy / strokeLen : 0.0f;
    float perpY = (strokeLen > 0.001f) ? dx / strokeLen : 0.0f;

    // Interpolate points
    for (int i = 0; i < numPoints; i++) {
        // Auto-flush: if buffer is getting full, render and commit current points
        // Leave some headroom (500 points) to avoid overflowing
        if (_points.size() >= metal_stamp::MAX_POINTS_PER_STROKE - 500) {
            NSLog(@"[Stroke] Auto-flush: %zu points at buffer limit, committing to canvas", _points.size());
            [self renderPointsWithHardness:self.strokeHardness opacity:self.strokeOpacity
                                      flow:self.strokeFlow grainScale:self.strokeGrainScale
                            useShapeTexture:self.strokeUseShape useGrainTexture:self.strokeUseGrain];
            [self commitStrokeToCanvas];  // This clears _points
        }

        float t = (float)i / (float)numPoints;

        // Base position
        simd_float2 pos = simd_make_float2(
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t
        );

        // Apply scatter (deterministic "random" offset perpendicular to stroke)
        if (scatter > 0.0f) {
            float r = deterministic_random(self.strokeRandomSeed, self.strokeRandomCounter++);
            float scatterAmount = (r - 0.5f) * 2.0f * scatter;
            float scatterNDC = scatterAmount * size / self.canvasWidth;
            pos.x += perpX * scatterNDC;
            pos.y += perpY * scatterNDC;
        }

        // Apply size jitter (deterministic)
        float finalSize = size;
        if (sizeJitter > 0.0f) {
            float r = deterministic_random(self.strokeRandomSeed, self.strokeRandomCounter++);
            float jitter = 1.0f + (r - 0.5f) * 2.0f * sizeJitter;
            finalSize *= jitter;
        }

        // Apply opacity jitter (deterministic)
        simd_float4 finalColor = color;
        if (opacityJitter > 0.0f) {
            float r = deterministic_random(self.strokeRandomSeed, self.strokeRandomCounter++);
            float jitter = 1.0f - r * opacityJitter;
            finalColor.w *= jitter;
        }

        MSLPoint point;
        point.position = pos;
        point.size = finalSize;
        point.color = finalColor;
        _points.push_back(point);
    }
}

- (simd_float4)backgroundColor {
    return _backgroundColor;
}

- (void)clearCanvasWithColor:(simd_float4)color {
    _backgroundColor = color;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = self.canvasTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(color.x, color.y, color.z, color.w);

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

- (void)renderPointsWithHardness:(float)hardness opacity:(float)opacity
                            flow:(float)flow grainScale:(float)grainScale
                  useShapeTexture:(BOOL)useShape useGrainTexture:(BOOL)useGrain {
    // Only render points we haven't rendered yet to avoid double-rendering
    // This is CRITICAL for deterministic replay - original drawing renders points
    // once as they're added, and replay must do the same
    size_t newPointCount = _points.size() - self.renderedPointCount;
    if (newPointCount == 0) return;

    // Update point buffer with ALL points (GPU needs contiguous buffer)
    memcpy(self.pointBuffer.contents, _points.data(), sizeof(MSLPoint) * _points.size());

    // Update uniforms - use canvas size since we're rendering to canvas texture
    MSLStrokeUniforms uniforms;
    uniforms.viewportSize = simd_make_float2(self.canvasWidth, self.canvasHeight);
    uniforms.hardness = hardness;
    uniforms.opacity = opacity;
    uniforms.flow = flow;
    uniforms.grainScale = grainScale;
    uniforms.grainOffset = self.grainOffset;
    uniforms.useShapeTexture = useShape ? 1 : 0;
    uniforms.useGrainTexture = useGrain ? 1 : 0;
    uniforms.shapeInverted = self.shapeInverted;
    memcpy(self.uniformBuffer.contents, &uniforms, sizeof(MSLStrokeUniforms));

    // Render to canvas texture
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = self.canvasTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;  // Preserve existing content
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

    // CRITICAL: Set viewport to canvas texture size, not screen size!
    // Without this, strokes outside the screen viewport won't be rendered to the 4K canvas
    MTLViewport viewport = {0, 0, (double)self.canvasWidth, (double)self.canvasHeight, 0.0, 1.0};
    [encoder setViewport:viewport];

    // Select pipeline based on brush type and textures
    id<MTLRenderPipelineState> pipeline = self.stampPipeline;  // Default

    // If we have a shape texture, use the textured pipeline to render it
    if (useShape && self.currentShapeTexture && self.stampTexturePipeline) {
        pipeline = self.stampTexturePipeline;
    } else {
        // Otherwise use the procedural brush type
        switch (self.currentBrushType) {
            case 0:  // Round
                pipeline = self.stampPipeline;
                break;
            case 1:  // Crayon
                pipeline = self.crayonPipeline ? self.crayonPipeline : self.stampPipeline;
                break;
            case 2:  // Watercolor
                pipeline = self.watercolorPipeline ? self.watercolorPipeline : self.stampPipeline;
                break;
            case 3:  // Marker
                pipeline = self.markerPipeline ? self.markerPipeline : self.stampPipeline;
                break;
        }
    }
    [encoder setRenderPipelineState:pipeline];
    [encoder setVertexBuffer:self.pointBuffer offset:0 atIndex:0];
    [encoder setVertexBuffer:self.uniformBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:self.uniformBuffer offset:0 atIndex:1];

    // Bind textures and sampler if available
    if (useShape && self.currentShapeTexture) {
        [encoder setFragmentTexture:self.currentShapeTexture atIndex:0];
        [encoder setFragmentSamplerState:self.textureSampler atIndex:0];
    }
    if (useGrain && self.currentGrainTexture) {
        [encoder setFragmentTexture:self.currentGrainTexture atIndex:1];
        [encoder setFragmentSamplerState:self.textureSampler atIndex:1];
    }

    // Draw only NEW points (from renderedPointCount to end)
    // This ensures each point is rendered exactly once, matching replay behavior
    [encoder drawPrimitives:MTLPrimitiveTypePoint
                vertexStart:self.renderedPointCount
                vertexCount:newPointCount];

    [encoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    // Update rendered count so these points won't be rendered again
    self.renderedPointCount = _points.size();
    self.pointsRendered = (int)_points.size();
}

- (void)commitStrokeToCanvas {
    // Points are already rendered to canvas texture in renderPointsWithHardness
    // Just clear the point buffer for the next stroke
    _points.clear();
    self.pointCount = 0;
    self.renderedPointCount = 0;  // Reset for next stroke
}

// =============================================================================
// UI Drawing Methods
// =============================================================================

- (void)queueUIRect:(float)x y:(float)y width:(float)w height:(float)h
              color:(simd_float4)color cornerRadius:(float)radius {
    // Convert screen coordinates to NDC
    // Screen: (0,0) at top-left, (width, height) at bottom-right
    // NDC: (-1,-1) at bottom-left, (1,1) at top-right
    float ndcX = (x / self.drawableWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / self.drawableHeight) * 2.0f;  // Flip Y
    float ndcW = (w / self.drawableWidth) * 2.0f;
    float ndcH = (h / self.drawableHeight) * 2.0f;

    // Convert corner radius to NDC (use average of width/height for aspect ratio)
    float ndcRadius = (radius / ((self.drawableWidth + self.drawableHeight) / 2.0f)) * 2.0f;

    UIRectParams params;
    params.rect = simd_make_float4(ndcX, ndcY - ndcH, ndcW, ndcH);  // Adjust Y for top-left origin
    params.color = color;
    params.cornerRadius = ndcRadius;

    _uiRects.push_back(params);
}

- (void)queueUITexturedRect:(float)x y:(float)y width:(float)w height:(float)h
                  textureId:(int32_t)textureId tint:(simd_float4)tint {
    // Convert screen coordinates to NDC
    float ndcX = (x / self.drawableWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / self.drawableHeight) * 2.0f;  // Flip Y
    float ndcW = (w / self.drawableWidth) * 2.0f;
    float ndcH = (h / self.drawableHeight) * 2.0f;

    UITexturedRectParams params;
    params.rect = simd_make_float4(ndcX, ndcY - ndcH, ndcW, ndcH);  // Adjust Y for top-left origin
    params.tint = tint;
    params.textureId = textureId;

    _uiTexturedRects.push_back(params);
}

- (void)drawQueuedUIRectsToTexture:(id<MTLTexture>)texture {
    if (_uiRects.empty() && _uiTexturedRects.empty()) return;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = texture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;  // Preserve existing content
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

    // Draw solid color rects
    if (!_uiRects.empty() && self.uiRectPipeline) {
        [encoder setRenderPipelineState:self.uiRectPipeline];
        for (const UIRectParams& params : _uiRects) {
            [encoder setVertexBytes:&params length:sizeof(UIRectParams) atIndex:0];
            [encoder setFragmentBytes:&params length:sizeof(UIRectParams) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }
    }

    // Draw textured rects
    if (!_uiTexturedRects.empty() && self.uiTexturedRectPipeline) {
        [encoder setRenderPipelineState:self.uiTexturedRectPipeline];
        [encoder setFragmentSamplerState:self.textureSampler atIndex:0];
        NSLog(@"[TexturedRect] Drawing %lu textured rects", _uiTexturedRects.size());
        for (const UITexturedRectParams& params : _uiTexturedRects) {
            id<MTLTexture> tex = [self getTextureById:params.textureId];
            if (tex) {
                [encoder setVertexBytes:&params length:sizeof(UITexturedRectParams) atIndex:0];
                [encoder setFragmentBytes:&params length:sizeof(UITexturedRectParams) atIndex:0];
                [encoder setFragmentTexture:tex atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            } else {
                NSLog(@"[TexturedRect] WARNING: texture %d not found!", params.textureId);
            }
        }
    } else if (!_uiTexturedRects.empty()) {
        NSLog(@"[TexturedRect] WARNING: %lu rects queued but pipeline is nil!", _uiTexturedRects.size());
    }

    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

- (void)clearUIQueue {
    _uiRects.clear();
    _uiTexturedRects.clear();
}

// =============================================================================
// Canvas Transform Methods
// =============================================================================

- (void)setCanvasTransformPanX:(float)panX panY:(float)panY
                         scale:(float)scale rotation:(float)rotation
                        pivotX:(float)pivotX pivotY:(float)pivotY {
    _canvasTransform.pan = simd_make_float2(panX, panY);
    _canvasTransform.scale = scale;
    _canvasTransform.rotation = rotation;
    // Pivot is in pixel coordinates (matches shader which works in pixel space)
    _canvasTransform.pivot = simd_make_float2(pivotX, pivotY);
    _canvasTransform.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);
    _canvasTransform.canvasSize = simd_make_float2(self.canvasWidth, self.canvasHeight);
}

- (void)drawCanvasToTexture:(id<MTLTexture>)targetTexture {
    if (!self.canvasBlitPipeline) {
        // Fallback: use direct blit if pipeline not available
        return;
    }

    // Update viewport and canvas sizes in case they changed
    _canvasTransform.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);
    _canvasTransform.canvasSize = simd_make_float2(self.canvasWidth, self.canvasHeight);

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = targetTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.3, 0.3, 0.3, 1.0);  // Gray background for out-of-bounds

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder setRenderPipelineState:self.canvasBlitPipeline];

    // Set transform uniforms
    [encoder setVertexBytes:&_canvasTransform length:sizeof(CanvasTransformUniforms) atIndex:0];

    // Set canvas texture and sampler
    [encoder setFragmentTexture:self.canvasTexture atIndex:0];
    [encoder setFragmentSamplerState:self.textureSampler atIndex:0];

    // Draw full-screen quad
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];

    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

- (BOOL)hasCanvasTransform {
    // Always return YES now that canvas can be different size from screen
    // This ensures the blit pipeline is used for proper scaling
    return YES;
}

// =============================================================================
// Canvas Snapshots
// =============================================================================

- (NSData*)captureCanvasSnapshot {
    if (!self.canvasTexture) return nil;

    int w = self.canvasWidth;
    int h = self.canvasHeight;
    size_t bytesPerRow = w * 4;
    size_t totalBytes = bytesPerRow * h;

    // Create buffer to hold pixel data
    NSMutableData* pixelData = [NSMutableData dataWithLength:totalBytes];
    if (!pixelData) return nil;

    // Read pixels from texture (synchronous)
    [self.canvasTexture getBytes:pixelData.mutableBytes
                     bytesPerRow:bytesPerRow
                      fromRegion:MTLRegionMake2D(0, 0, w, h)
                     mipmapLevel:0];

    std::cout << "[Snapshot] Captured " << w << "x" << h << " (" << totalBytes << " bytes)" << std::endl;
    return pixelData;
}

- (BOOL)restoreCanvasSnapshot:(NSData*)pixels width:(int)width height:(int)height {
    if (!self.canvasTexture || !pixels) return NO;

    int texW = self.canvasWidth;
    int texH = self.canvasHeight;

    // Check if dimensions match
    if (width != texW || height != texH) {
        std::cout << "[Snapshot] Size mismatch: snapshot " << width << "x" << height
                  << ", canvas " << texW << "x" << texH << std::endl;
        return NO;
    }

    size_t bytesPerRow = width * 4;
    size_t expectedSize = bytesPerRow * height;
    if (pixels.length != expectedSize) {
        std::cout << "[Snapshot] Data size mismatch: " << pixels.length
                  << " vs expected " << expectedSize << std::endl;
        return NO;
    }

    // Write pixels to texture
    [self.canvasTexture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                          mipmapLevel:0
                            withBytes:pixels.bytes
                          bytesPerRow:bytesPerRow];

    return YES;
}

// =============================================================================
// Delta Snapshots - Capture/restore only a region of the canvas
// =============================================================================

- (NSData*)captureDeltaSnapshotX:(int)x y:(int)y width:(int)w height:(int)h {
    if (!self.canvasTexture) return nil;

    int canvasW = self.canvasWidth;
    int canvasH = self.canvasHeight;

    // Clamp region to canvas bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > canvasW) w = canvasW - x;
    if (y + h > canvasH) h = canvasH - y;

    if (w <= 0 || h <= 0) return nil;

    size_t bytesPerRow = w * 4;
    size_t totalBytes = bytesPerRow * h;

    NSMutableData* pixelData = [NSMutableData dataWithLength:totalBytes];
    if (!pixelData) return nil;

    // Read only the specified region
    [self.canvasTexture getBytes:pixelData.mutableBytes
                     bytesPerRow:bytesPerRow
                      fromRegion:MTLRegionMake2D(x, y, w, h)
                     mipmapLevel:0];

    return pixelData;
}

- (BOOL)restoreDeltaSnapshot:(NSData*)pixels atX:(int)x y:(int)y width:(int)w height:(int)h {
    if (!self.canvasTexture || !pixels) return NO;

    int canvasW = self.canvasWidth;
    int canvasH = self.canvasHeight;

    // Validate bounds
    if (x < 0 || y < 0 || x + w > canvasW || y + h > canvasH) {
        std::cout << "[Delta] Region out of bounds: (" << x << "," << y << ") "
                  << w << "x" << h << " vs canvas " << canvasW << "x" << canvasH << std::endl;
        return NO;
    }

    size_t bytesPerRow = w * 4;
    size_t expectedSize = bytesPerRow * h;
    if (pixels.length != expectedSize) {
        std::cout << "[Delta] Size mismatch: " << pixels.length
                  << " vs expected " << expectedSize << std::endl;
        return NO;
    }

    // Write pixels to the specified region
    [self.canvasTexture replaceRegion:MTLRegionMake2D(x, y, w, h)
                          mipmapLevel:0
                            withBytes:pixels.bytes
                          bytesPerRow:bytesPerRow];

    return YES;
}

// =============================================================================
// Frame Texture Cache - For instant animation frame switching
// =============================================================================

- (BOOL)initFrameCache:(int)maxFrames {
    if (!self.device || !self.canvasTexture) return NO;

    self.maxFrames = maxFrames;
    self.activeFrameIndex = 0;
    self.frameTextureCache = [NSMutableArray arrayWithCapacity:maxFrames];
    self.frameCacheValid = [NSMutableArray arrayWithCapacity:maxFrames];

    // Pre-allocate textures for all frames (same size as canvas - 4K resolution)
    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:self.canvasWidth
                                    height:self.canvasHeight
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;  // GPU-only for fastest access

    for (int i = 0; i < maxFrames; i++) {
        id<MTLTexture> frameTexture = [self.device newTextureWithDescriptor:desc];
        if (!frameTexture) {
            std::cout << "[FrameCache] Failed to create texture for frame " << i << std::endl;
            return NO;
        }
        [self.frameTextureCache addObject:frameTexture];
        [self.frameCacheValid addObject:@NO];  // Not cached yet
    }

    std::cout << "[FrameCache] Initialized " << maxFrames << " frame textures ("
              << self.canvasWidth << "x" << self.canvasHeight << ")" << std::endl;
    return YES;
}

- (BOOL)cacheCurrentFrameToGPU:(int)frameIndex {
    if (!self.frameTextureCache || frameIndex < 0 || frameIndex >= self.maxFrames) return NO;
    if (!self.canvasTexture) return NO;

    id<MTLTexture> targetTexture = self.frameTextureCache[frameIndex];
    if (!targetTexture) return NO;

    // Use blit encoder to copy canvas to frame texture (GPU-to-GPU, very fast)
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];

    [blitEncoder copyFromTexture:self.canvasTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(self.canvasWidth, self.canvasHeight, 1)
                       toTexture:targetTexture
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];

    [blitEncoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];  // Ensure copy is done

    // Mark as valid
    self.frameCacheValid[frameIndex] = @YES;
    return YES;
}

- (BOOL)switchToCachedFrame:(int)frameIndex {
    if (!self.frameTextureCache || frameIndex < 0 || frameIndex >= self.maxFrames) return NO;

    id<MTLTexture> sourceTexture = self.frameTextureCache[frameIndex];
    if (!sourceTexture || !self.canvasTexture) return NO;

    // Copy cached frame to canvas (GPU-to-GPU, instant - no CPU involved!)
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];

    [blitEncoder copyFromTexture:sourceTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(self.canvasWidth, self.canvasHeight, 1)
                       toTexture:self.canvasTexture
                destinationSlice:0
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];

    [blitEncoder endEncoding];
    [commandBuffer commit];
    // Don't wait - let GPU do it asynchronously for smoothest experience

    self.activeFrameIndex = frameIndex;
    return YES;
}

- (BOOL)isFrameCached:(int)frameIndex {
    if (!self.frameTextureCache || !self.frameCacheValid) return NO;
    if (frameIndex < 0 || frameIndex >= self.maxFrames) return NO;
    return [self.frameCacheValid[frameIndex] boolValue];
}

- (void)clearFrameCache {
    [self.frameTextureCache removeAllObjects];
    self.frameTextureCache = nil;
    [self.frameCacheValid removeAllObjects];
    self.frameCacheValid = nil;
    self.maxFrames = 0;
    self.activeFrameIndex = 0;
}

@end

// =============================================================================
// C++ Implementation (inside namespace)
// =============================================================================

namespace metal_stamp {

// =============================================================================
// MetalStampRenderer C++ Implementation
// =============================================================================

MetalStampRenderer::MetalStampRenderer()
    : impl_(nullptr)
    , initialized_(false)
    , width_(0)
    , height_(0) {
    // Set default brush settings
    brush_.size = 20.0f;
    brush_.hardness = 0.0f;
    brush_.opacity = 1.0f;
    brush_.spacing = 0.15f;
    brush_.r = 0.0f;
    brush_.g = 0.0f;
    brush_.b = 0.0f;
    brush_.a = 1.0f;
}

MetalStampRenderer::~MetalStampRenderer() {
    cleanup();
}

bool MetalStampRenderer::init(void* sdl_window, int width, int height) {
    if (initialized_) {
        std::cerr << "[MetalStampRenderer] Already initialized" << std::endl;
        return false;
    }

    impl_ = [[MetalStampRendererImpl alloc] init];
    if (![impl_ initWithWindow:(SDL_Window*)sdl_window width:width height:height]) {
        impl_ = nil;
        return false;
    }

    // Store canvas dimensions (not screen dimensions)
    width_ = impl_.canvasWidth;
    height_ = impl_.canvasHeight;
    initialized_ = true;
    return true;
}

bool MetalStampRenderer::is_ready() const {
    return initialized_ && impl_ != nil;
}

void MetalStampRenderer::cleanup() {
    if (impl_) {
        [impl_ cleanup];
        impl_ = nil;
    }
    initialized_ = false;
}

void MetalStampRenderer::set_brush_type(BrushType type) {
    brush_.type = type;
    if (impl_) {
        impl_.currentBrushType = static_cast<int>(type);
    }
}

void MetalStampRenderer::set_brush_size(float size) {
    brush_.size = size;
}

void MetalStampRenderer::set_brush_hardness(float hardness) {
    brush_.hardness = hardness;
}

void MetalStampRenderer::set_brush_opacity(float opacity) {
    brush_.opacity = opacity;
}

void MetalStampRenderer::set_brush_spacing(float spacing) {
    brush_.spacing = spacing;
}

void MetalStampRenderer::set_brush_color(float r, float g, float b, float a) {
    brush_.r = r;
    brush_.g = g;
    brush_.b = b;
    brush_.a = a;
}

void MetalStampRenderer::set_brush(const BrushSettings& settings) {
    brush_ = settings;

    // CRITICAL: Also update impl_ properties that are used during rendering
    // Without this, replay would use the CURRENT brush type/textures instead of recorded ones
    if (is_ready()) {
        impl_.currentBrushType = static_cast<int>(settings.type);
        impl_.currentShapeTexture = [impl_ getTextureById:settings.shape_texture_id];
        impl_.currentGrainTexture = [impl_ getTextureById:settings.grain_texture_id];
        impl_.shapeInverted = settings.shape_inverted;
    }
}

BrushSettings MetalStampRenderer::get_brush() const {
    return brush_;
}

void MetalStampRenderer::set_brush_rotation(float degrees) {
    brush_.rotation = degrees;
}

void MetalStampRenderer::set_brush_rotation_jitter(float degrees) {
    brush_.rotation_jitter = degrees;
}

void MetalStampRenderer::set_brush_scatter(float scatter) {
    brush_.scatter = scatter;
}

void MetalStampRenderer::set_brush_size_pressure(float amount) {
    brush_.size_pressure = amount;
}

void MetalStampRenderer::set_brush_opacity_pressure(float amount) {
    brush_.opacity_pressure = amount;
}

void MetalStampRenderer::set_brush_size_jitter(float amount) {
    brush_.size_jitter = amount;
}

void MetalStampRenderer::set_brush_opacity_jitter(float amount) {
    brush_.opacity_jitter = amount;
}

int32_t MetalStampRenderer::load_texture(const char* path) {
    if (!is_ready()) return 0;
    NSString* nsPath = [NSString stringWithUTF8String:path];
    return [impl_ loadTextureFromFile:nsPath];
}

int32_t MetalStampRenderer::load_texture_from_data(const uint8_t* data, int width, int height) {
    if (!is_ready()) return 0;
    return [impl_ loadTextureFromData:data width:width height:height];
}

int32_t MetalStampRenderer::load_texture_from_rgba_data(const uint8_t* data, int width, int height) {
    if (!is_ready()) return 0;
    return [impl_ loadTextureFromRGBAData:data width:width height:height];
}

void MetalStampRenderer::set_brush_shape_texture(int32_t texture_id) {
    brush_.shape_texture_id = texture_id;
    if (is_ready()) {
        impl_.currentShapeTexture = [impl_ getTextureById:texture_id];
    }
}

void MetalStampRenderer::set_brush_grain_texture(int32_t texture_id) {
    brush_.grain_texture_id = texture_id;
    if (is_ready()) {
        impl_.currentGrainTexture = [impl_ getTextureById:texture_id];
    }
}

void MetalStampRenderer::set_brush_grain_scale(float scale) {
    brush_.grain_scale = scale;
}

void MetalStampRenderer::set_brush_grain_moving(bool moving) {
    brush_.grain_moving = moving;
}

void MetalStampRenderer::set_brush_shape_inverted(int inverted) {
    brush_.shape_inverted = inverted;
    if (is_ready()) {
        impl_.shapeInverted = inverted;
    }
}

void MetalStampRenderer::unload_texture(int32_t texture_id) {
    if (!is_ready()) return;
    [impl_ unloadTexture:texture_id];
}

void MetalStampRenderer::set_stroke_random_seed(uint32_t seed) {
    if (!is_ready()) return;
    impl_.strokeRandomSeed = seed;
    impl_.strokeRandomCounter = 0;  // Reset counter for new stroke
}

void MetalStampRenderer::begin_stroke(float x, float y, float pressure) {
    if (!is_ready()) return;

    impl_.isDrawing = YES;
    impl_.renderedPointCount = 0;  // Reset for new stroke
    impl_.lastPoint = [impl_ screenToNDC:x y:y];

    // Store brush settings for auto-flush during long strokes
    BOOL useShape = brush_.shape_texture_id != 0 && impl_.currentShapeTexture != nil;
    BOOL useGrain = brush_.grain_texture_id != 0 && impl_.currentGrainTexture != nil;
    impl_.strokeHardness = brush_.hardness;
    impl_.strokeOpacity = brush_.opacity;
    impl_.strokeFlow = brush_.flow;
    impl_.strokeGrainScale = brush_.grain_scale;
    impl_.strokeUseShape = useShape;
    impl_.strokeUseGrain = useGrain;

    // Apply pressure dynamics
    // size_pressure: 0 = constant size, 1 = full pressure variation
    // opacity_pressure: 0 = constant opacity, 1 = full pressure variation
    float sizeFactor = 1.0f - brush_.size_pressure + (brush_.size_pressure * pressure);
    float opacityFactor = 1.0f - brush_.opacity_pressure + (brush_.opacity_pressure * pressure);

    float effectiveSize = brush_.size * sizeFactor;
    float effectiveAlpha = brush_.a * opacityFactor;

    simd_float4 color = simd_make_float4(brush_.r, brush_.g, brush_.b, effectiveAlpha);
    [impl_ interpolateFrom:impl_.lastPoint to:impl_.lastPoint
                 pointSize:effectiveSize color:color spacing:brush_.spacing
                   scatter:brush_.scatter sizeJitter:brush_.size_jitter opacityJitter:brush_.opacity_jitter];

    impl_.pointCount = 1;
}

void MetalStampRenderer::add_stroke_point(float x, float y, float pressure) {
    if (!is_ready() || !impl_.isDrawing) return;

    // NOTE: Auto-flush is now handled in interpolateFrom:to: where _points.size() is accessible

    // Apply pressure dynamics
    float sizeFactor = 1.0f - brush_.size_pressure + (brush_.size_pressure * pressure);
    float opacityFactor = 1.0f - brush_.opacity_pressure + (brush_.opacity_pressure * pressure);

    float effectiveSize = brush_.size * sizeFactor;
    float effectiveAlpha = brush_.a * opacityFactor;

    simd_float2 newPoint = [impl_ screenToNDC:x y:y];
    simd_float4 color = simd_make_float4(brush_.r, brush_.g, brush_.b, effectiveAlpha);

    [impl_ interpolateFrom:impl_.lastPoint to:newPoint
                 pointSize:effectiveSize color:color spacing:brush_.spacing
                   scatter:brush_.scatter sizeJitter:brush_.size_jitter opacityJitter:brush_.opacity_jitter];

    impl_.lastPoint = newPoint;
    impl_.pointCount++;
}

void MetalStampRenderer::end_stroke() {
    if (!is_ready() || !impl_.isDrawing) return;

    // Render remaining points to canvas
    BOOL useShape = brush_.shape_texture_id != 0 && impl_.currentShapeTexture != nil;
    BOOL useGrain = brush_.grain_texture_id != 0 && impl_.currentGrainTexture != nil;
    [impl_ renderPointsWithHardness:brush_.hardness opacity:brush_.opacity
                               flow:brush_.flow grainScale:brush_.grain_scale
                     useShapeTexture:useShape useGrainTexture:useGrain];
    [impl_ commitStrokeToCanvas];

    impl_.isDrawing = NO;
}

void MetalStampRenderer::cancel_stroke() {
    if (!is_ready()) return;

    // Just clear points without rendering
    [impl_ commitStrokeToCanvas];
    impl_.isDrawing = NO;
}

void MetalStampRenderer::clear_canvas() {
    if (!is_ready()) return;
    // Uses the stored background color from impl_
    [impl_ clearCanvasWithColor:[impl_ backgroundColor]];
}

void MetalStampRenderer::clear_canvas(const Color& color) {
    if (!is_ready()) return;

    simd_float4 c = simd_make_float4(color.r, color.g, color.b, color.a);
    [impl_ clearCanvasWithColor:c];
}

Color MetalStampRenderer::get_background_color() const {
    simd_float4 bg = [impl_ backgroundColor];
    return Color(bg.x, bg.y, bg.z, bg.w);
}

void* MetalStampRenderer::get_canvas_sdl_texture() {
    // TODO: Implement SDL texture interop
    return nullptr;
}

void MetalStampRenderer::resize(int width, int height) {
    if (!is_ready()) return;

    width_ = width;
    height_ = height;
    [impl_ resizeWithWidth:width height:height];
}

std::vector<uint8_t> MetalStampRenderer::capture_canvas_snapshot() {
    if (!is_ready()) return {};

    NSData* data = [impl_ captureCanvasSnapshot];
    if (!data) return {};

    std::vector<uint8_t> result(data.length);
    memcpy(result.data(), data.bytes, data.length);
    return result;
}

bool MetalStampRenderer::restore_canvas_snapshot(const std::vector<uint8_t>& pixels, int width, int height) {
    if (!is_ready() || pixels.empty()) return false;

    NSData* data = [NSData dataWithBytes:pixels.data() length:pixels.size()];
    return [impl_ restoreCanvasSnapshot:data width:width height:height];
}

std::vector<uint8_t> MetalStampRenderer::capture_delta_snapshot(int x, int y, int w, int h) {
    if (!is_ready()) return {};

    NSData* data = [impl_ captureDeltaSnapshotX:x y:y width:w height:h];
    if (!data) return {};

    std::vector<uint8_t> result(data.length);
    memcpy(result.data(), data.bytes, data.length);
    return result;
}

bool MetalStampRenderer::restore_delta_snapshot(const std::vector<uint8_t>& pixels, int x, int y, int w, int h) {
    if (!is_ready() || pixels.empty()) return false;

    NSData* data = [NSData dataWithBytes:pixels.data() length:pixels.size()];
    return [impl_ restoreDeltaSnapshot:data atX:x y:y width:w height:h];
}

void MetalStampRenderer::render_current_stroke() {
    if (!is_ready() || !impl_.isDrawing) return;

    // Render current stroke points
    BOOL useShape = brush_.shape_texture_id != 0 && impl_.currentShapeTexture != nil;
    BOOL useGrain = brush_.grain_texture_id != 0 && impl_.currentGrainTexture != nil;
    [impl_ renderPointsWithHardness:brush_.hardness opacity:brush_.opacity
                               flow:brush_.flow grainScale:brush_.grain_scale
                     useShapeTexture:useShape useGrainTexture:useGrain];
}

void MetalStampRenderer::present() {
    if (!is_ready()) return;

    // Get next drawable from Metal layer
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl_.metalLayer nextDrawable];
        if (!drawable) {
            return;
        }

        // Check if we need to use transformed draw or direct blit
        if ([impl_ hasCanvasTransform] && impl_.canvasBlitPipeline) {
            // Use transformed canvas draw
            [impl_ drawCanvasToTexture:drawable.texture];
        } else {
            // Use direct blit (no transform)
            id<MTLCommandBuffer> commandBuffer = [impl_.commandQueue commandBuffer];

            id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
            NSUInteger srcWidth = impl_.canvasTexture.width;
            NSUInteger srcHeight = impl_.canvasTexture.height;
            [blitEncoder copyFromTexture:impl_.canvasTexture
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(srcWidth, srcHeight, 1)
                               toTexture:drawable.texture
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blitEncoder endEncoding];

            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
        }

        // Draw UI overlays on drawable (after canvas completes)
        [impl_ drawQueuedUIRectsToTexture:drawable.texture];
        [impl_ clearUIQueue];

        // Present the drawable
        id<MTLCommandBuffer> presentBuffer = [impl_.commandQueue commandBuffer];
        [presentBuffer presentDrawable:drawable];
        [presentBuffer commit];
    }
}

int MetalStampRenderer::get_current_stroke_point_count() const {
    return is_ready() ? impl_.pointCount : 0;
}

int MetalStampRenderer::get_points_rendered() const {
    return is_ready() ? impl_.pointsRendered : 0;
}

void MetalStampRenderer::queue_ui_rect(float x, float y, float width, float height,
                                       float r, float g, float b, float a,
                                       float corner_radius) {
    if (!is_ready()) return;

    simd_float4 color = simd_make_float4(r, g, b, a);
    [impl_ queueUIRect:x y:y width:width height:height color:color cornerRadius:corner_radius];
}

void MetalStampRenderer::queue_ui_textured_rect(float x, float y, float width, float height,
                                                int32_t texture_id,
                                                float tint_r, float tint_g, float tint_b, float alpha) {
    if (!is_ready()) return;

    simd_float4 tint = simd_make_float4(tint_r, tint_g, tint_b, alpha);
    [impl_ queueUITexturedRect:x y:y width:width height:height textureId:texture_id tint:tint];
}

void MetalStampRenderer::set_canvas_transform(float panX, float panY, float scale,
                                              float rotation, float pivotX, float pivotY) {
    if (!is_ready()) return;

    [impl_ setCanvasTransformPanX:panX panY:panY scale:scale rotation:rotation
                          pivotX:pivotX pivotY:pivotY];
}

// =============================================================================
// Frame Cache Methods - For instant animation frame switching (GPU-to-GPU)
// =============================================================================

bool MetalStampRenderer::init_frame_cache(int maxFrames) {
    if (!is_ready()) return false;
    return [impl_ initFrameCache:maxFrames];
}

bool MetalStampRenderer::cache_frame_to_gpu(int frameIndex) {
    if (!is_ready()) return false;
    return [impl_ cacheCurrentFrameToGPU:frameIndex];
}

bool MetalStampRenderer::switch_to_cached_frame(int frameIndex) {
    if (!is_ready()) return false;
    return [impl_ switchToCachedFrame:frameIndex];
}

bool MetalStampRenderer::is_frame_cached(int frameIndex) {
    if (!is_ready()) return false;
    return [impl_ isFrameCached:frameIndex];
}

void MetalStampRenderer::clear_frame_cache() {
    if (!is_ready()) return;
    [impl_ clearFrameCache];
}

// =============================================================================
// Global Renderer Instance
// =============================================================================

static MetalStampRenderer* g_metal_renderer = nullptr;

// =============================================================================
// Undo Tree - Global instance and current stroke tracking
// =============================================================================

// Per-frame undo trees - each frame has its own independent undo history
// Configurable: can support multiple animations with different frame counts
static std::vector<std::unique_ptr<undo_tree::UndoTree>> g_undo_trees;
static int g_current_undo_frame = 0;  // Which frame's undo tree is active
static bool g_undo_initialized = false;

// Helper to get current frame's undo tree
static undo_tree::UndoTree* get_current_undo_tree() {
    if (!g_undo_initialized) return nullptr;
    if (g_current_undo_frame < 0 || g_current_undo_frame >= (int)g_undo_trees.size()) return nullptr;
    return g_undo_trees[g_current_undo_frame].get();
}

static undo_tree::StrokeData g_current_stroke;  // Accumulates points during drawing
static bool g_is_recording_stroke = false;

// Stroke bounding box (kept for potential future delta optimization)
static float g_stroke_min_x = 0, g_stroke_min_y = 0;
static float g_stroke_max_x = 0, g_stroke_max_y = 0;
static bool g_stroke_bbox_valid = false;

// Debug: Log when dylib is loaded
__attribute__((constructor))
static void metal_dylib_init() {
    printf("[metal_dylib_init] dylib loaded!\n");
    fflush(stdout);
}

// Simple test function to verify linking works
extern "C" __attribute__((visibility("default"))) __attribute__((used))
int metal_test_add(int a, int b) {
    printf("[metal_test_add] called with %d + %d\n", a, b);
    fflush(stdout);
    return a + b;
}

bool init_metal_renderer(void* sdl_window, int width, int height) {
    std::cout << "[init_metal_renderer] ENTER" << std::endl;
    std::cout.flush();

    if (g_metal_renderer) {
        std::cerr << "[MetalStampRenderer] Global renderer already initialized" << std::endl;
        return false;
    }

    std::cout << "[init_metal_renderer] Creating MetalStampRenderer..." << std::endl;
    std::cout.flush();
    g_metal_renderer = new MetalStampRenderer();

    std::cout << "[init_metal_renderer] Calling init()..." << std::endl;
    std::cout.flush();
    if (!g_metal_renderer->init(sdl_window, width, height)) {
        std::cout << "[init_metal_renderer] init() FAILED" << std::endl;
        std::cout.flush();
        delete g_metal_renderer;
        g_metal_renderer = nullptr;
        return false;
    }

    std::cout << "[init_metal_renderer] SUCCESS" << std::endl;
    std::cout.flush();
    return true;
}

void cleanup_metal_renderer() {
    if (g_metal_renderer) {
        g_metal_renderer->cleanup();
        delete g_metal_renderer;
        g_metal_renderer = nullptr;
    }
}

MetalStampRenderer* get_metal_renderer() {
    return g_metal_renderer;
}

bool is_metal_available() {
    return g_metal_renderer != nullptr && g_metal_renderer->is_ready();
}

// Brush functions
void metal_set_brush_type(int32_t type) {
    if (g_metal_renderer) g_metal_renderer->set_brush_type(static_cast<metal_stamp::BrushType>(type));
}

void metal_set_brush_size(float size) {
    if (g_metal_renderer) g_metal_renderer->set_brush_size(size);
}

void metal_set_brush_hardness(float hardness) {
    if (g_metal_renderer) g_metal_renderer->set_brush_hardness(hardness);
}

void metal_set_brush_opacity(float opacity) {
    if (g_metal_renderer) g_metal_renderer->set_brush_opacity(opacity);
}

void metal_set_brush_spacing(float spacing) {
    if (g_metal_renderer) g_metal_renderer->set_brush_spacing(spacing);
}

void metal_set_brush_color(float r, float g, float b, float a) {
    if (g_metal_renderer) g_metal_renderer->set_brush_color(r, g, b, a);
}

// Stroke functions
void metal_begin_stroke(float x, float y, float pressure) {
    if (g_metal_renderer) g_metal_renderer->begin_stroke(x, y, pressure);
}

void metal_add_stroke_point(float x, float y, float pressure) {
    if (g_metal_renderer) g_metal_renderer->add_stroke_point(x, y, pressure);
}

void metal_end_stroke() {
    if (g_metal_renderer) g_metal_renderer->end_stroke();
}

void metal_cancel_stroke() {
    if (g_metal_renderer) g_metal_renderer->cancel_stroke();
}

// Canvas functions
void metal_clear_canvas(float r, float g, float b, float a) {
    if (g_metal_renderer) g_metal_renderer->clear_canvas(Color(r, g, b, a));
}

void metal_render_stroke() {
    if (g_metal_renderer) g_metal_renderer->render_current_stroke();
}

void metal_present() {
    if (g_metal_renderer) g_metal_renderer->present();
}

// UI Drawing
void metal_queue_ui_rect(float x, float y, float width, float height,
                         float r, float g, float b, float a,
                         float corner_radius) {
    if (g_metal_renderer) g_metal_renderer->queue_ui_rect(x, y, width, height, r, g, b, a, corner_radius);
}

void metal_queue_ui_textured_rect(float x, float y, float width, float height,
                                  int32_t texture_id,
                                  float tint_r, float tint_g, float tint_b, float alpha) {
    if (g_metal_renderer) g_metal_renderer->queue_ui_textured_rect(x, y, width, height, texture_id, tint_r, tint_g, tint_b, alpha);
}

// Canvas Transform
void metal_set_canvas_transform(float panX, float panY, float scale,
                                float rotation, float pivotX, float pivotY) {
    if (g_metal_renderer) g_metal_renderer->set_canvas_transform(panX, panY, scale, rotation, pivotX, pivotY);
}

// Extended brush settings
void metal_set_brush_rotation(float degrees) {
    if (g_metal_renderer) g_metal_renderer->set_brush_rotation(degrees);
}

void metal_set_brush_rotation_jitter(float degrees) {
    if (g_metal_renderer) g_metal_renderer->set_brush_rotation_jitter(degrees);
}

void metal_set_brush_scatter(float scatter) {
    if (g_metal_renderer) g_metal_renderer->set_brush_scatter(scatter);
}

void metal_set_brush_size_pressure(float amount) {
    if (g_metal_renderer) g_metal_renderer->set_brush_size_pressure(amount);
}

void metal_set_brush_opacity_pressure(float amount) {
    if (g_metal_renderer) g_metal_renderer->set_brush_opacity_pressure(amount);
}

void metal_set_brush_size_jitter(float amount) {
    if (g_metal_renderer) g_metal_renderer->set_brush_size_jitter(amount);
}

void metal_set_brush_opacity_jitter(float amount) {
    if (g_metal_renderer) g_metal_renderer->set_brush_opacity_jitter(amount);
}

// Texture management
int32_t metal_load_texture(const char* path) {
    return g_metal_renderer ? g_metal_renderer->load_texture(path) : 0;
}

int32_t metal_load_texture_data(const uint8_t* data, int width, int height) {
    return g_metal_renderer ? g_metal_renderer->load_texture_from_data(data, width, height) : 0;
}

int32_t metal_load_rgba_texture_data(const uint8_t* data, int width, int height) {
    return g_metal_renderer ? g_metal_renderer->load_texture_from_rgba_data(data, width, height) : 0;
}

void metal_set_brush_shape_texture(int32_t texture_id) {
    if (g_metal_renderer) g_metal_renderer->set_brush_shape_texture(texture_id);
}

void metal_set_brush_grain_texture(int32_t texture_id) {
    if (g_metal_renderer) g_metal_renderer->set_brush_grain_texture(texture_id);
}

void metal_set_brush_grain_scale(float scale) {
    if (g_metal_renderer) g_metal_renderer->set_brush_grain_scale(scale);
}

void metal_set_brush_grain_moving(bool moving) {
    if (g_metal_renderer) g_metal_renderer->set_brush_grain_moving(moving);
}

void metal_set_brush_shape_inverted(int inverted) {
    if (g_metal_renderer) g_metal_renderer->set_brush_shape_inverted(inverted);
}

void metal_unload_texture(int32_t texture_id) {
    if (g_metal_renderer) g_metal_renderer->unload_texture(texture_id);
}

} // namespace metal_stamp

// =============================================================================
// extern "C" wrapper functions for JIT linking
// These have C linkage for predictable symbol names
// Use visibility("default") and used attribute to ensure symbols are exported
// =============================================================================

#define METAL_EXPORT __attribute__((visibility("default"))) __attribute__((used))

extern "C" {

METAL_EXPORT bool metal_stamp_init(void* sdl_window, int width, int height) {
    std::cout << "[metal_stamp_init] ENTER sdl_window=" << sdl_window << " size=" << width << "x" << height << std::endl;
    std::cout.flush();
    bool result = metal_stamp::init_metal_renderer(sdl_window, width, height);
    std::cout << "[metal_stamp_init] EXIT result=" << result << std::endl;
    std::cout.flush();
    return result;
}

METAL_EXPORT void metal_stamp_cleanup() {
    metal_stamp::cleanup_metal_renderer();
}

METAL_EXPORT bool metal_stamp_is_available() {
    return metal_stamp::is_metal_available();
}

METAL_EXPORT void metal_stamp_set_brush_type(int32_t type) {
    metal_stamp::metal_set_brush_type(type);
}

METAL_EXPORT void metal_stamp_set_brush_size(float size) {
    metal_stamp::metal_set_brush_size(size);
}

METAL_EXPORT void metal_stamp_set_brush_hardness(float hardness) {
    metal_stamp::metal_set_brush_hardness(hardness);
}

METAL_EXPORT void metal_stamp_set_brush_opacity(float opacity) {
    metal_stamp::metal_set_brush_opacity(opacity);
}

METAL_EXPORT void metal_stamp_set_brush_spacing(float spacing) {
    metal_stamp::metal_set_brush_spacing(spacing);
}

METAL_EXPORT void metal_stamp_set_brush_color(float r, float g, float b, float a) {
    metal_stamp::metal_set_brush_color(r, g, b, a);
}

METAL_EXPORT void metal_stamp_begin_stroke(float x, float y, float pressure) {
    metal_stamp::metal_begin_stroke(x, y, pressure);
}

METAL_EXPORT void metal_stamp_add_stroke_point(float x, float y, float pressure) {
    metal_stamp::metal_add_stroke_point(x, y, pressure);
}

METAL_EXPORT void metal_stamp_end_stroke() {
    metal_stamp::metal_end_stroke();
}

METAL_EXPORT void metal_stamp_cancel_stroke() {
    metal_stamp::metal_cancel_stroke();
}

METAL_EXPORT void metal_stamp_clear_canvas(float r, float g, float b, float a) {
    metal_stamp::metal_clear_canvas(r, g, b, a);
}

// Get background color (r, g, b, a returned via output pointers)
METAL_EXPORT void metal_stamp_get_background_color(float* r, float* g, float* b, float* a) {
    auto color = metal_stamp::g_metal_renderer->get_background_color();
    *r = color.r;
    *g = color.g;
    *b = color.b;
    *a = color.a;
}

METAL_EXPORT void metal_stamp_render_stroke() {
    metal_stamp::metal_render_stroke();
}

METAL_EXPORT void metal_stamp_present() {
    metal_stamp::metal_present();
}

// UI Drawing
METAL_EXPORT void metal_stamp_queue_ui_rect(float x, float y, float width, float height,
                                            float r, float g, float b, float a,
                                            float corner_radius) {
    metal_stamp::metal_queue_ui_rect(x, y, width, height, r, g, b, a, corner_radius);
}

METAL_EXPORT void metal_stamp_queue_ui_textured_rect(float x, float y, float width, float height,
                                                     int32_t texture_id,
                                                     float tint_r, float tint_g, float tint_b, float alpha) {
    metal_stamp::metal_queue_ui_textured_rect(x, y, width, height, texture_id, tint_r, tint_g, tint_b, alpha);
}

// Canvas Transform
METAL_EXPORT void metal_stamp_set_canvas_transform(float panX, float panY, float scale,
                                                   float rotation, float pivotX, float pivotY) {
    metal_stamp::metal_set_canvas_transform(panX, panY, scale, rotation, pivotX, pivotY);
}

// Extended brush settings
METAL_EXPORT void metal_stamp_set_brush_rotation(float degrees) {
    metal_stamp::metal_set_brush_rotation(degrees);
}

METAL_EXPORT void metal_stamp_set_brush_rotation_jitter(float degrees) {
    metal_stamp::metal_set_brush_rotation_jitter(degrees);
}

METAL_EXPORT void metal_stamp_set_brush_scatter(float scatter) {
    metal_stamp::metal_set_brush_scatter(scatter);
}

METAL_EXPORT void metal_stamp_set_brush_size_pressure(float amount) {
    metal_stamp::metal_set_brush_size_pressure(amount);
}

METAL_EXPORT void metal_stamp_set_brush_opacity_pressure(float amount) {
    metal_stamp::metal_set_brush_opacity_pressure(amount);
}

METAL_EXPORT void metal_stamp_set_brush_size_jitter(float amount) {
    metal_stamp::metal_set_brush_size_jitter(amount);
}

METAL_EXPORT void metal_stamp_set_brush_opacity_jitter(float amount) {
    metal_stamp::metal_set_brush_opacity_jitter(amount);
}

// Texture management
METAL_EXPORT int32_t metal_stamp_load_texture(const char* path) {
    return metal_stamp::metal_load_texture(path);
}

METAL_EXPORT int32_t metal_stamp_load_texture_data(const uint8_t* data, int width, int height) {
    return metal_stamp::metal_load_texture_data(data, width, height);
}

METAL_EXPORT int32_t metal_stamp_load_rgba_texture_data(const uint8_t* data, int width, int height) {
    return metal_stamp::metal_load_rgba_texture_data(data, width, height);
}

METAL_EXPORT void metal_stamp_set_brush_shape_texture(int32_t texture_id) {
    metal_stamp::metal_set_brush_shape_texture(texture_id);
}

METAL_EXPORT void metal_stamp_set_brush_grain_texture(int32_t texture_id) {
    metal_stamp::metal_set_brush_grain_texture(texture_id);
}

METAL_EXPORT void metal_stamp_set_brush_grain_scale(float scale) {
    metal_stamp::metal_set_brush_grain_scale(scale);
}

METAL_EXPORT void metal_stamp_set_brush_grain_moving(bool moving) {
    metal_stamp::metal_set_brush_grain_moving(moving);
}

METAL_EXPORT void metal_stamp_set_brush_shape_inverted(int inverted) {
    metal_stamp::metal_set_brush_shape_inverted(inverted);
}

METAL_EXPORT void metal_stamp_unload_texture(int32_t texture_id) {
    metal_stamp::metal_unload_texture(texture_id);
}

// Built-in brush presets (procedural, no textures needed)
METAL_EXPORT void metal_stamp_use_preset_round_soft() {
    metal_stamp::metal_set_brush_hardness(0.0f);
    metal_stamp::metal_set_brush_spacing(0.1f);
    metal_stamp::metal_set_brush_shape_texture(0);  // Procedural circle
    metal_stamp::metal_set_brush_grain_texture(0);  // No grain
}

METAL_EXPORT void metal_stamp_use_preset_round_hard() {
    metal_stamp::metal_set_brush_hardness(0.9f);
    metal_stamp::metal_set_brush_spacing(0.1f);
    metal_stamp::metal_set_brush_shape_texture(0);  // Procedural circle
    metal_stamp::metal_set_brush_grain_texture(0);  // No grain
}

METAL_EXPORT void metal_stamp_use_preset_square() {
    // For now, just a hard brush (square texture would need to be loaded)
    metal_stamp::metal_set_brush_hardness(1.0f);
    metal_stamp::metal_set_brush_spacing(0.1f);
    metal_stamp::metal_set_brush_shape_texture(0);
    metal_stamp::metal_set_brush_grain_texture(0);
}

METAL_EXPORT void metal_stamp_use_preset_splatter() {
    metal_stamp::metal_set_brush_hardness(0.3f);
    metal_stamp::metal_set_brush_spacing(0.3f);
    metal_stamp::metal_set_brush_rotation_jitter(180.0f);  // Random rotation
    metal_stamp::metal_set_brush_scatter(0.2f);            // Slight scatter
    metal_stamp::metal_set_brush_shape_texture(0);
    metal_stamp::metal_set_brush_grain_texture(0);
}

// =============================================================================
// Undo Tree API Implementation
// =============================================================================

// Helper to configure an undo tree with all callbacks
static void configure_undo_tree(undo_tree::UndoTree* tree) {
    if (!tree) return;

    // Pure replay approach - NO snapshots needed since replay is now deterministic
    // Saves ~2 GB of memory (was: 12 frames × 5 snapshots × 33 MB)
    // Undo/redo clears canvas and replays all strokes from root (fast on GPU)
    tree->setMaxNodes(50);         // 50 undo levels per frame
    tree->setSnapshotInterval(0);  // DISABLED - pure replay, no snapshots

    // Snapshot callback - capture full canvas
    tree->setSnapshotCallback([]() -> std::shared_ptr<undo_tree::CanvasSnapshot> {
        if (!metal_stamp::g_metal_renderer) return nullptr;

        int canvasW = metal_stamp::g_metal_renderer->get_canvas_width();
        int canvasH = metal_stamp::g_metal_renderer->get_canvas_height();

        auto snapshot = std::make_shared<undo_tree::CanvasSnapshot>();
        snapshot->timestamp = SDL_GetTicks();
        snapshot->canvasWidth = canvasW;
        snapshot->canvasHeight = canvasH;
        snapshot->isDelta = false;
        snapshot->deltaX = 0;
        snapshot->deltaY = 0;
        snapshot->width = canvasW;
        snapshot->height = canvasH;
        snapshot->pixels = metal_stamp::g_metal_renderer->capture_canvas_snapshot();

        if (snapshot->pixels.empty()) {
            std::cout << "[UndoTree] Failed to capture snapshot" << std::endl;
            return nullptr;
        }

        std::cout << "[UndoTree] Snapshot " << snapshot->width << "x"
                  << snapshot->height << " (" << snapshot->pixels.size() / 1024 << " KB)" << std::endl;
        return snapshot;
    });

    // Restore callback - restore full canvas from snapshot
    tree->setRestoreCallback([](const undo_tree::CanvasSnapshot& snapshot) {
        if (!metal_stamp::g_metal_renderer) return;

        std::cout << "[UndoTree] Restoring snapshot " << snapshot.width << "x"
                  << snapshot.height << std::endl;
        metal_stamp::g_metal_renderer->restore_canvas_snapshot(
            snapshot.pixels, snapshot.width, snapshot.height);
    });

    // Clear callback - for when we need to go back to empty canvas (root)
    tree->setClearCallback([]() {
        std::cout << "[UndoTree] Clearing canvas (back to root)" << std::endl;
        if (metal_stamp::g_metal_renderer) {
            metal_stamp::g_metal_renderer->clear_canvas();  // Uses stored background color
        }
    });

    // Stroke replay callback - replays a stroke from recorded data
    tree->setApplyStrokeCallback([](const undo_tree::StrokeData& stroke) {
        if (!metal_stamp::g_metal_renderer || stroke.points.empty()) return;

        // Save current brush
        auto savedBrush = metal_stamp::g_metal_renderer->get_brush();

        // Apply stroke's brush settings
        metal_stamp::BrushSettings brush;
        brush.type = static_cast<metal_stamp::BrushType>(stroke.brush.brushType);
        brush.size = stroke.brush.size;
        brush.hardness = stroke.brush.hardness;
        brush.opacity = stroke.brush.opacity;
        brush.spacing = stroke.brush.spacing;
        brush.flow = stroke.brush.flow;
        brush.r = stroke.brush.r;
        brush.g = stroke.brush.g;
        brush.b = stroke.brush.b;
        brush.a = stroke.brush.a;
        brush.shape_texture_id = stroke.brush.shape_texture_id;
        brush.grain_texture_id = stroke.brush.grain_texture_id;
        brush.grain_scale = stroke.brush.grain_scale;
        brush.grain_moving = stroke.brush.grain_moving;
        brush.shape_inverted = stroke.brush.shape_inverted;
        brush.rotation = stroke.brush.rotation;
        brush.rotation_jitter = stroke.brush.rotation_jitter;
        brush.scatter = stroke.brush.scatter;
        brush.size_pressure = stroke.brush.size_pressure;
        brush.opacity_pressure = stroke.brush.opacity_pressure;
        brush.size_velocity = stroke.brush.size_velocity;
        brush.size_jitter = stroke.brush.size_jitter;
        brush.opacity_jitter = stroke.brush.opacity_jitter;
        metal_stamp::g_metal_renderer->set_brush(brush);

        // Set deterministic random seed BEFORE begin_stroke for reproducible jitter/scatter
        metal_stamp::g_metal_renderer->set_stroke_random_seed(stroke.randomSeed);

        // Replay stroke: begin -> add points -> end
        const auto& first = stroke.points[0];
        metal_stamp::g_metal_renderer->begin_stroke(first.x, first.y, first.pressure);

        // Add remaining points
        for (size_t i = 1; i < stroke.points.size(); i++) {
            const auto& pt = stroke.points[i];
            metal_stamp::g_metal_renderer->add_stroke_point(pt.x, pt.y, pt.pressure);
        }

        // End stroke - renders and commits to canvas
        metal_stamp::g_metal_renderer->end_stroke();

        // Restore original brush
        metal_stamp::g_metal_renderer->set_brush(savedBrush);

        std::cout << "[UndoTree] Replayed stroke with " << stroke.points.size() << " points" << std::endl;
    });
}

// Initialize undo system with specified number of frames
// Each frame gets its own independent undo tree
METAL_EXPORT void metal_stamp_undo_init_with_frames(int num_frames) {
    using namespace metal_stamp;
    if (g_undo_initialized) return;  // Already initialized
    if (num_frames <= 0) num_frames = 12;  // Default to 12

    // Create and configure undo tree for each frame
    g_undo_trees.clear();
    g_undo_trees.resize(num_frames);
    for (int i = 0; i < num_frames; i++) {
        g_undo_trees[i] = std::make_unique<undo_tree::UndoTree>();
        configure_undo_tree(g_undo_trees[i].get());
    }

    g_current_undo_frame = 0;
    g_undo_initialized = true;

    std::cout << "[UndoTree] Initialized " << num_frames << " per-frame undo trees "
              << "(50 levels each, checkpoints every 10 strokes)" << std::endl;
}

// Legacy init - defaults to 12 frames
METAL_EXPORT void metal_stamp_undo_init() {
    metal_stamp_undo_init_with_frames(12);
}

METAL_EXPORT void metal_stamp_undo_cleanup() {
    using namespace metal_stamp;
    g_undo_trees.clear();
    g_undo_initialized = false;
    g_current_undo_frame = 0;
    g_current_stroke = undo_tree::StrokeData();
    g_is_recording_stroke = false;
}

// Switch to a different frame's undo tree
// Call this when frame changes (frame_next, frame_prev, frame_goto)
METAL_EXPORT void metal_stamp_undo_set_frame(int frame) {
    using namespace metal_stamp;
    if (!g_undo_initialized) return;
    if (frame < 0 || frame >= (int)g_undo_trees.size()) {
        std::cout << "[UndoTree] Invalid frame index: " << frame
                  << " (max: " << g_undo_trees.size() << ")" << std::endl;
        return;
    }
    g_current_undo_frame = frame;
    std::cout << "[UndoTree] Switched to frame " << frame << std::endl;
}

// Get current frame for undo
METAL_EXPORT int metal_stamp_undo_get_frame() {
    return metal_stamp::g_current_undo_frame;
}

// Get number of frames
METAL_EXPORT int metal_stamp_undo_get_frame_count() {
    return (int)metal_stamp::g_undo_trees.size();
}

METAL_EXPORT void metal_stamp_undo_set_max_nodes(int max) {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (tree) tree->setMaxNodes(max);
}

METAL_EXPORT void metal_stamp_undo_set_snapshot_interval(int interval) {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (tree) tree->setSnapshotInterval(interval);
}

METAL_EXPORT bool metal_stamp_undo() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (!tree) {
        std::cout << "[UndoTree] metal_stamp_undo called but tree not initialized!" << std::endl;
        return false;
    }
    std::cout << "[UndoTree] Frame " << g_current_undo_frame << " undo, depth: "
              << tree->getCurrentDepth() << ", total: " << tree->getTotalNodes() << std::endl;
    bool result = tree->undo();
    std::cout << "[UndoTree] Result: " << (result ? "success" : "failed")
              << ", new depth: " << tree->getCurrentDepth() << std::endl;
    return result;
}

METAL_EXPORT bool metal_stamp_redo() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (!tree) return false;
    return tree->redo();
}

METAL_EXPORT bool metal_stamp_redo_branch(int branch) {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (!tree) return false;
    return tree->redoBranch(branch);
}

METAL_EXPORT bool metal_stamp_undo_jump_to(uint64_t nodeId) {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (!tree) return false;
    return tree->jumpToNode(nodeId);
}

METAL_EXPORT bool metal_stamp_can_undo() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree && tree->canUndo();
}

METAL_EXPORT bool metal_stamp_can_redo() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree && tree->canRedo();
}

METAL_EXPORT int metal_stamp_get_redo_branch_count() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree ? tree->getRedoBranchCount() : 0;
}

METAL_EXPORT uint64_t metal_stamp_get_current_node_id() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree ? tree->getCurrentId() : 0;
}

METAL_EXPORT int metal_stamp_get_undo_depth() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree ? tree->getCurrentDepth() : 0;
}

METAL_EXPORT int metal_stamp_get_total_undo_nodes() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    return tree ? tree->getTotalNodes() : 0;
}

METAL_EXPORT void metal_stamp_undo_print_tree() {
    using namespace metal_stamp;
    auto* tree = get_current_undo_tree();
    if (!tree) {
        std::cout << "[UndoTree] Not initialized" << std::endl;
        return;
    }

    std::cout << "[UndoTree] Frame " << g_current_undo_frame
              << ": nodes=" << tree->getTotalNodes()
              << ", depth=" << tree->getCurrentDepth()
              << ", nodeId=" << tree->getCurrentId()
              << ", canUndo=" << (tree->canUndo() ? "yes" : "no")
              << ", canRedo=" << (tree->canRedo() ? "yes" : "no")
              << ", branches=" << tree->getRedoBranchCount() << std::endl;
}

// =============================================================================
// Undo-aware stroke recording (internal helpers)
// =============================================================================

namespace metal_stamp {

// Called when a stroke begins - start recording points AND draw
void undo_begin_stroke(float x, float y, float pressure) {
    // Generate random seed for deterministic replay of jitter/scatter
    // Uses timestamp + position - fully deterministic, no rand() involved!
    uint32_t ticks = (uint32_t)SDL_GetTicks();
    uint32_t strokeSeed = ticks ^ ((uint32_t)(x * 1000) << 16) ^ ((uint32_t)(y * 1000));

    // ALWAYS draw, even if undo tree not initialized
    if (g_metal_renderer) {
        g_metal_renderer->set_stroke_random_seed(strokeSeed);  // Set seed before begin!
        g_metal_renderer->begin_stroke(x, y, pressure);
    }

    // Record to undo tree if available (per-frame)
    auto* tree = get_current_undo_tree();
    if (!tree) return;

    g_current_stroke = undo_tree::StrokeData();
    g_current_stroke.startTime = SDL_GetTicks();
    g_current_stroke.randomSeed = strokeSeed;  // Store for deterministic replay

    // Record ALL brush settings from the current renderer brush
    if (g_metal_renderer) {
        auto brush = g_metal_renderer->get_brush();
        // Core settings
        g_current_stroke.brush.brushType = static_cast<int>(brush.type);
        g_current_stroke.brush.size = brush.size;
        g_current_stroke.brush.hardness = brush.hardness;
        g_current_stroke.brush.opacity = brush.opacity;
        g_current_stroke.brush.spacing = brush.spacing;
        g_current_stroke.brush.flow = brush.flow;
        g_current_stroke.brush.r = brush.r;
        g_current_stroke.brush.g = brush.g;
        g_current_stroke.brush.b = brush.b;
        g_current_stroke.brush.a = brush.a;
        // Texture settings
        g_current_stroke.brush.shape_texture_id = brush.shape_texture_id;
        g_current_stroke.brush.grain_texture_id = brush.grain_texture_id;
        g_current_stroke.brush.grain_scale = brush.grain_scale;
        g_current_stroke.brush.grain_moving = brush.grain_moving;
        g_current_stroke.brush.shape_inverted = brush.shape_inverted;
        // Dynamics
        g_current_stroke.brush.rotation = brush.rotation;
        g_current_stroke.brush.rotation_jitter = brush.rotation_jitter;
        g_current_stroke.brush.scatter = brush.scatter;
        g_current_stroke.brush.size_pressure = brush.size_pressure;
        g_current_stroke.brush.opacity_pressure = brush.opacity_pressure;
        g_current_stroke.brush.size_velocity = brush.size_velocity;
        g_current_stroke.brush.size_jitter = brush.size_jitter;
        g_current_stroke.brush.opacity_jitter = brush.opacity_jitter;
    }

    // Add first point
    undo_tree::StrokePoint pt;
    pt.x = x;
    pt.y = y;
    pt.pressure = pressure;
    pt.timestamp = 0;  // Relative to stroke start
    g_current_stroke.points.push_back(pt);

    // Initialize bounding box with first point (expanded by MAX possible brush size)
    float baseSize = g_current_stroke.brush.size;
    float pressureMax = 1.0f + g_current_stroke.brush.size_pressure;
    float jitterMax = 1.0f + g_current_stroke.brush.size_jitter;
    float scatterMax = baseSize * g_current_stroke.brush.scatter;
    float maxSize = baseSize * pressureMax * jitterMax + scatterMax;

    g_stroke_min_x = x - maxSize;
    g_stroke_min_y = y - maxSize;
    g_stroke_max_x = x + maxSize;
    g_stroke_max_y = y + maxSize;
    g_stroke_bbox_valid = true;

    g_is_recording_stroke = true;
}

// Called for each stroke point - record it AND draw
void undo_add_stroke_point(float x, float y, float pressure) {
    // ALWAYS draw
    if (g_metal_renderer) {
        g_metal_renderer->add_stroke_point(x, y, pressure);
    }

    // Record to undo tree
    if (!g_is_recording_stroke) return;

    undo_tree::StrokePoint pt;
    pt.x = x;
    pt.y = y;
    pt.pressure = pressure;
    pt.timestamp = SDL_GetTicks() - g_current_stroke.startTime;

    // Expand bounding box (account for MAX possible brush size due to dynamics)
    // size_pressure can increase size based on pressure (0-1), size_jitter adds random variation
    float baseSize = g_current_stroke.brush.size;
    float pressureMax = 1.0f + g_current_stroke.brush.size_pressure;  // Pressure can double size
    float jitterMax = 1.0f + g_current_stroke.brush.size_jitter;      // Jitter can also increase
    float scatterMax = baseSize * g_current_stroke.brush.scatter;     // Scatter offsets stamps
    float maxSize = baseSize * pressureMax * jitterMax + scatterMax;

    if (x - maxSize < g_stroke_min_x) g_stroke_min_x = x - maxSize;
    if (y - maxSize < g_stroke_min_y) g_stroke_min_y = y - maxSize;
    if (x + maxSize > g_stroke_max_x) g_stroke_max_x = x + maxSize;
    if (y + maxSize > g_stroke_max_y) g_stroke_max_y = y + maxSize;
    g_current_stroke.points.push_back(pt);
}

// Called when stroke ends - commit to undo tree AND finalize drawing
void undo_end_stroke() {
    // ALWAYS finalize the stroke drawing
    if (g_metal_renderer) {
        g_metal_renderer->end_stroke();
    }

    auto* tree = get_current_undo_tree();
    if (!g_is_recording_stroke || !tree) {
        g_is_recording_stroke = false;
        return;
    }

    // Only record if we have points (to current frame's undo tree)
    if (!g_current_stroke.isEmpty()) {
        tree->recordStroke(g_current_stroke);
    }

    g_current_stroke = undo_tree::StrokeData();
    g_is_recording_stroke = false;
}

// Called when stroke is cancelled - discard AND cancel drawing
void undo_cancel_stroke() {
    // Cancel the drawing stroke too
    if (g_metal_renderer) {
        g_metal_renderer->cancel_stroke();
    }

    g_current_stroke = undo_tree::StrokeData();
    g_is_recording_stroke = false;
    g_stroke_bbox_valid = false;
}

} // namespace metal_stamp

// C API wrappers for undo-aware stroke recording
METAL_EXPORT void metal_stamp_undo_begin_stroke(float x, float y, float pressure) {
    metal_stamp::undo_begin_stroke(x, y, pressure);
}

METAL_EXPORT void metal_stamp_undo_add_stroke_point(float x, float y, float pressure) {
    metal_stamp::undo_add_stroke_point(x, y, pressure);
}

METAL_EXPORT void metal_stamp_undo_end_stroke() {
    metal_stamp::undo_end_stroke();
}

METAL_EXPORT void metal_stamp_undo_cancel_stroke() {
    metal_stamp::undo_cancel_stroke();
}

// Canvas snapshot functions for frame-based animation
METAL_EXPORT int metal_stamp_capture_snapshot(uint8_t** out_pixels) {
    if (!metal_stamp::g_metal_renderer || !out_pixels) return 0;
    auto snapshot = metal_stamp::g_metal_renderer->capture_canvas_snapshot();
    if (snapshot.empty()) return 0;

    int size = (int)snapshot.size();
    *out_pixels = (uint8_t*)malloc(size);
    if (*out_pixels) {
        memcpy(*out_pixels, snapshot.data(), size);
    }
    return size;
}

METAL_EXPORT void metal_stamp_restore_snapshot(const uint8_t* pixels, int size, int width, int height) {
    if (!metal_stamp::g_metal_renderer || !pixels || size <= 0) return;
    std::vector<uint8_t> data(pixels, pixels + size);
    metal_stamp::g_metal_renderer->restore_canvas_snapshot(data, width, height);
}

METAL_EXPORT void metal_stamp_free_snapshot(uint8_t* pixels) {
    if (pixels) free(pixels);
}

// =============================================================================
// Frame Cache API - For instant animation frame switching
// =============================================================================

METAL_EXPORT bool metal_stamp_init_frame_cache(int maxFrames) {
    if (!metal_stamp::g_metal_renderer) return false;
    return metal_stamp::g_metal_renderer->init_frame_cache(maxFrames);
}

METAL_EXPORT bool metal_stamp_cache_frame_to_gpu(int frameIndex) {
    if (!metal_stamp::g_metal_renderer) return false;
    return metal_stamp::g_metal_renderer->cache_frame_to_gpu(frameIndex);
}

METAL_EXPORT bool metal_stamp_switch_to_cached_frame(int frameIndex) {
    if (!metal_stamp::g_metal_renderer) return false;
    return metal_stamp::g_metal_renderer->switch_to_cached_frame(frameIndex);
}

METAL_EXPORT bool metal_stamp_is_frame_cached(int frameIndex) {
    if (!metal_stamp::g_metal_renderer) return false;
    return metal_stamp::g_metal_renderer->is_frame_cached(frameIndex);
}

METAL_EXPORT void metal_stamp_clear_frame_cache() {
    if (!metal_stamp::g_metal_renderer) return;
    metal_stamp::g_metal_renderer->clear_frame_cache();
}

METAL_EXPORT int metal_stamp_get_canvas_width() {
    if (!metal_stamp::g_metal_renderer) return 0;
    return metal_stamp::g_metal_renderer->get_canvas_width();
}

METAL_EXPORT int metal_stamp_get_canvas_height() {
    if (!metal_stamp::g_metal_renderer) return 0;
    return metal_stamp::g_metal_renderer->get_canvas_height();
}

} // extern "C"
