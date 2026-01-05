// Metal Stamp Renderer - Objective-C++ Implementation
// Implements GPU stamp rendering using Metal for iOS/macOS

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include "metal_renderer.h"
#include <vector>
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
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, id<MTLTexture>>* loadedTextures;
@property (nonatomic, assign) int32_t nextTextureId;

@property (nonatomic, assign) int width;           // Point size (for input)
@property (nonatomic, assign) int height;          // Point size (for input)
@property (nonatomic, assign) int drawableWidth;   // Pixel size (for rendering)
@property (nonatomic, assign) int drawableHeight;  // Pixel size (for rendering)
@property (nonatomic, assign) BOOL isDrawing;
@property (nonatomic, assign) simd_float2 lastPoint;
@property (nonatomic, assign) simd_float2 grainOffset;  // Accumulated grain offset for moving mode
@property (nonatomic, assign) int pointCount;
@property (nonatomic, assign) int pointsRendered;

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
- (id<MTLTexture>)getTextureById:(int32_t)textureId;
- (void)unloadTexture:(int32_t)textureId;

// Rendering
- (void)clearCanvasWithColor:(simd_float4)color;
- (void)renderPointsWithHardness:(float)hardness opacity:(float)opacity
                            flow:(float)flow grainScale:(float)grainScale
                  useShapeTexture:(BOOL)useShape useGrainTexture:(BOOL)useGrain;
- (void)commitStrokeToCanvas;

// UI Drawing
- (void)queueUIRect:(float)x y:(float)y width:(float)w height:(float)h
              color:(simd_float4)color cornerRadius:(float)radius;
- (void)drawQueuedUIRectsToTexture:(id<MTLTexture>)texture;
- (void)clearUIQueue;

// Canvas Transform
- (void)setCanvasTransformPanX:(float)panX panY:(float)panY
                         scale:(float)scale rotation:(float)rotation
                        pivotX:(float)pivotX pivotY:(float)pivotY;
- (void)drawCanvasToTexture:(id<MTLTexture>)targetTexture;
- (BOOL)hasCanvasTransform;

@end

// UI Rect parameters (matches shader struct)
struct UIRectParams {
    simd_float4 rect;       // x, y, width, height in NDC
    simd_float4 color;
    float cornerRadius;
    float _padding[3];      // Align to 16 bytes
};

// Canvas transform uniforms (matches shader struct)
struct CanvasTransformUniforms {
    simd_float2 pan;           // Pan offset in screen pixels
    float scale;               // Zoom level (1.0 = 100%)
    float rotation;            // Rotation in radians
    simd_float2 pivot;         // Transform pivot in pixels
    simd_float2 viewportSize;  // Viewport size for aspect ratio
};

@implementation MetalStampRendererImpl {
    std::vector<MSLPoint> _points;
    simd_float4 _backgroundColor;
    std::vector<UIRectParams> _uiRects;  // UI rects to draw this frame
    CanvasTransformUniforms _canvasTransform;  // Canvas pan/zoom/rotate
}

- (BOOL)initWithWindow:(SDL_Window*)window width:(int)w height:(int)h {
    self.width = w;
    self.height = h;
    self.isDrawing = NO;
    self.pointCount = 0;
    self.pointsRendered = 0;
    self.grainOffset = simd_make_float2(0.0f, 0.0f);
    self.nextTextureId = 1;  // 0 is reserved for "no texture"
    self.currentBrushType = 1;  // Default to Crayon brush!
    self.loadedTextures = [NSMutableDictionary dictionary];
    self.currentShapeTexture = nil;
    self.currentGrainTexture = nil;
    _backgroundColor = simd_make_float4(0.95f, 0.95f, 0.92f, 1.0f);

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
    METAL_LOG("Point size: %dx%d, Drawable size: %dx%d", w, h, self.drawableWidth, self.drawableHeight);

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

    // Enable alpha blending (Porter-Duff "over")
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

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
    // Use drawable size for canvas texture (accounts for Retina scale)
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:self.drawableWidth
                                    height:self.drawableHeight
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;

    self.canvasTexture = [self.device newTextureWithDescriptor:desc];
    if (!self.canvasTexture) {
        METAL_LOG("Failed to create canvas texture");
        return NO;
    }

    // Clear to background color
    [self clearCanvasWithColor:_backgroundColor];

    METAL_LOG("Created canvas texture (%dx%d)", self.drawableWidth, self.drawableHeight);
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

- (id<MTLTexture>)getTextureById:(int32_t)textureId {
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
    // Convert from screen pixels to Metal NDC (-1 to 1)
    // Note: Metal Y axis is flipped (positive Y is up)
    float ndcX = (x / self.width) * 2.0f - 1.0f;
    float ndcY = 1.0f - (y / self.height) * 2.0f;  // Flip Y
    return simd_make_float2(ndcX, ndcY);
}

- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing
                scatter:(float)scatter sizeJitter:(float)sizeJitter opacityJitter:(float)opacityJitter {
    // Calculate distance in screen space
    float dx = (to.x - from.x) * self.width * 0.5f;
    float dy = (to.y - from.y) * self.height * 0.5f;
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
    for (int i = 0; i < numPoints && _points.size() < metal_stamp::MAX_POINTS_PER_STROKE; i++) {
        float t = (float)i / (float)numPoints;

        // Base position
        simd_float2 pos = simd_make_float2(
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t
        );

        // Apply scatter (random offset perpendicular to stroke)
        if (scatter > 0.0f) {
            float scatterAmount = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * scatter;
            float scatterNDC = scatterAmount * size / self.width;  // Convert to NDC
            pos.x += perpX * scatterNDC;
            pos.y += perpY * scatterNDC;
        }

        // Apply size jitter
        float finalSize = size;
        if (sizeJitter > 0.0f) {
            float jitter = 1.0f + ((float)rand() / RAND_MAX - 0.5f) * 2.0f * sizeJitter;
            finalSize *= jitter;
        }

        // Apply opacity jitter
        simd_float4 finalColor = color;
        if (opacityJitter > 0.0f) {
            float jitter = 1.0f - ((float)rand() / RAND_MAX) * opacityJitter;
            finalColor.w *= jitter;
        }

        MSLPoint point;
        point.position = pos;
        point.size = finalSize;
        point.color = finalColor;
        _points.push_back(point);
    }
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
    if (_points.empty()) return;

    // Update point buffer
    memcpy(self.pointBuffer.contents, _points.data(), sizeof(MSLPoint) * _points.size());

    // Update uniforms - use drawable size for proper Retina support
    MSLStrokeUniforms uniforms;
    uniforms.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);
    uniforms.hardness = hardness;
    uniforms.opacity = opacity;
    uniforms.flow = flow;
    uniforms.grainScale = grainScale;
    uniforms.grainOffset = self.grainOffset;
    uniforms.useShapeTexture = useShape ? 1 : 0;
    uniforms.useGrainTexture = useGrain ? 1 : 0;
    memcpy(self.uniformBuffer.contents, &uniforms, sizeof(MSLStrokeUniforms));

    // Render to canvas texture
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = self.canvasTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;  // Preserve existing content
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

    // Select pipeline based on brush type
    id<MTLRenderPipelineState> pipeline = self.stampPipeline;  // Default
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

    // Draw points
    [encoder drawPrimitives:MTLPrimitiveTypePoint
                vertexStart:0
                vertexCount:_points.size()];

    [encoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    self.pointsRendered = (int)_points.size();
}

- (void)commitStrokeToCanvas {
    // Points are already rendered to canvas texture in renderPointsWithHardness
    // Just clear the point buffer for the next stroke
    _points.clear();
    self.pointCount = 0;
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

- (void)drawQueuedUIRectsToTexture:(id<MTLTexture>)texture {
    if (_uiRects.empty() || !self.uiRectPipeline) return;

    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = texture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;  // Preserve existing content
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder setRenderPipelineState:self.uiRectPipeline];

    for (const UIRectParams& params : _uiRects) {
        [encoder setVertexBytes:&params length:sizeof(UIRectParams) atIndex:0];
        [encoder setFragmentBytes:&params length:sizeof(UIRectParams) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }

    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

- (void)clearUIQueue {
    _uiRects.clear();
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
}

- (void)drawCanvasToTexture:(id<MTLTexture>)targetTexture {
    if (!self.canvasBlitPipeline) {
        // Fallback: use direct blit if pipeline not available
        return;
    }

    // Update viewport size in case it changed
    _canvasTransform.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);

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
    // Check if canvas has any transform applied
    return _canvasTransform.scale != 1.0f ||
           _canvasTransform.rotation != 0.0f ||
           _canvasTransform.pan.x != 0.0f ||
           _canvasTransform.pan.y != 0.0f;
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

    width_ = width;
    height_ = height;
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

void MetalStampRenderer::unload_texture(int32_t texture_id) {
    if (!is_ready()) return;
    [impl_ unloadTexture:texture_id];
}

void MetalStampRenderer::begin_stroke(float x, float y, float pressure) {
    if (!is_ready()) return;

    impl_.isDrawing = YES;
    impl_.lastPoint = [impl_ screenToNDC:x y:y];

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

void MetalStampRenderer::clear_canvas(float r, float g, float b, float a) {
    if (!is_ready()) return;

    simd_float4 color = simd_make_float4(r, g, b, a);
    [impl_ clearCanvasWithColor:color];
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

void MetalStampRenderer::set_canvas_transform(float panX, float panY, float scale,
                                              float rotation, float pivotX, float pivotY) {
    if (!is_ready()) return;

    [impl_ setCanvasTransformPanX:panX panY:panY scale:scale rotation:rotation
                          pivotX:pivotX pivotY:pivotY];
}

// =============================================================================
// Global Renderer Instance
// =============================================================================

static MetalStampRenderer* g_metal_renderer = nullptr;

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
    if (g_metal_renderer) g_metal_renderer->clear_canvas(r, g, b, a);
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

} // extern "C"
