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
};

// =============================================================================
// MetalStampRendererImpl - Objective-C++ Implementation
// Must be at global scope (Objective-C requirement)
// =============================================================================

@interface MetalStampRendererImpl : NSObject

@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLRenderPipelineState> stampPipeline;
@property (nonatomic, strong) id<MTLRenderPipelineState> clearPipeline;
@property (nonatomic, strong) id<MTLTexture> canvasTexture;
@property (nonatomic, strong) id<MTLBuffer> pointBuffer;
@property (nonatomic, strong) id<MTLBuffer> uniformBuffer;
@property (nonatomic, assign) CAMetalLayer* metalLayer;

@property (nonatomic, assign) int width;           // Point size (for input)
@property (nonatomic, assign) int height;          // Point size (for input)
@property (nonatomic, assign) int drawableWidth;   // Pixel size (for rendering)
@property (nonatomic, assign) int drawableHeight;  // Pixel size (for rendering)
@property (nonatomic, assign) BOOL isDrawing;
@property (nonatomic, assign) simd_float2 lastPoint;
@property (nonatomic, assign) int pointCount;
@property (nonatomic, assign) int pointsRendered;

- (BOOL)initWithWindow:(SDL_Window*)window width:(int)w height:(int)h;
- (void)cleanup;
- (BOOL)createPipelines;
- (BOOL)createCanvasTexture;
- (void)resizeWithWidth:(int)w height:(int)h;

// Coordinate conversion
- (simd_float2)screenToNDC:(float)x y:(float)y;

// Point interpolation
- (void)interpolateFrom:(simd_float2)from to:(simd_float2)to
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing;

// Rendering
- (void)clearCanvasWithColor:(simd_float4)color;
- (void)renderPointsWithHardness:(float)hardness opacity:(float)opacity;
- (void)commitStrokeToCanvas;

@end

@implementation MetalStampRendererImpl {
    std::vector<MSLPoint> _points;
    simd_float4 _backgroundColor;
}

- (BOOL)initWithWindow:(SDL_Window*)window width:(int)w height:(int)h {
    self.width = w;
    self.height = h;
    self.isDrawing = NO;
    self.pointCount = 0;
    self.pointsRendered = 0;
    _backgroundColor = simd_make_float4(0.95f, 0.95f, 0.92f, 1.0f);

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
              pointSize:(float)size color:(simd_float4)color spacing:(float)spacing {
    // Calculate distance in screen space
    float dx = (to.x - from.x) * self.width * 0.5f;
    float dy = (to.y - from.y) * self.height * 0.5f;
    float distance = sqrtf(dx * dx + dy * dy);

    // Calculate number of points needed
    float stepSize = size * spacing;
    if (stepSize < 1.0f) stepSize = 1.0f;

    int numPoints = (int)(distance / stepSize);
    if (numPoints < 1) numPoints = 1;

    // Interpolate points
    for (int i = 0; i < numPoints && _points.size() < metal_stamp::MAX_POINTS_PER_STROKE; i++) {
        float t = (float)i / (float)numPoints;
        simd_float2 pos = simd_make_float2(
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t
        );

        MSLPoint point;
        point.position = pos;
        point.size = size;
        point.color = color;
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

- (void)renderPointsWithHardness:(float)hardness opacity:(float)opacity {
    if (_points.empty()) return;

    // Update point buffer
    memcpy(self.pointBuffer.contents, _points.data(), sizeof(MSLPoint) * _points.size());

    // Update uniforms - use drawable size for proper Retina support
    MSLStrokeUniforms uniforms;
    uniforms.viewportSize = simd_make_float2(self.drawableWidth, self.drawableHeight);
    uniforms.hardness = hardness;
    uniforms.opacity = opacity;
    memcpy(self.uniformBuffer.contents, &uniforms, sizeof(MSLStrokeUniforms));

    // Render to canvas texture
    id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = self.canvasTexture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;  // Preserve existing content
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];

    [encoder setRenderPipelineState:self.stampPipeline];
    [encoder setVertexBuffer:self.pointBuffer offset:0 atIndex:0];
    [encoder setVertexBuffer:self.uniformBuffer offset:0 atIndex:1];
    [encoder setFragmentBuffer:self.uniformBuffer offset:0 atIndex:1];

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

void MetalStampRenderer::begin_stroke(float x, float y, float pressure) {
    if (!is_ready()) return;

    impl_.isDrawing = YES;
    impl_.lastPoint = [impl_ screenToNDC:x y:y];

    // Add initial point
    simd_float4 color = simd_make_float4(brush_.r, brush_.g, brush_.b, brush_.a);
    [impl_ interpolateFrom:impl_.lastPoint to:impl_.lastPoint
                 pointSize:brush_.size * pressure color:color spacing:brush_.spacing];

    impl_.pointCount = 1;
}

void MetalStampRenderer::add_stroke_point(float x, float y, float pressure) {
    if (!is_ready() || !impl_.isDrawing) return;

    simd_float2 newPoint = [impl_ screenToNDC:x y:y];
    simd_float4 color = simd_make_float4(brush_.r, brush_.g, brush_.b, brush_.a);

    [impl_ interpolateFrom:impl_.lastPoint to:newPoint
                 pointSize:brush_.size * pressure color:color spacing:brush_.spacing];

    impl_.lastPoint = newPoint;
    impl_.pointCount++;
}

void MetalStampRenderer::end_stroke() {
    if (!is_ready() || !impl_.isDrawing) return;

    // Render remaining points to canvas
    [impl_ renderPointsWithHardness:brush_.hardness opacity:brush_.opacity];
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
    [impl_ renderPointsWithHardness:brush_.hardness opacity:brush_.opacity];
}

void MetalStampRenderer::present() {
    if (!is_ready()) return;

    // Get next drawable from Metal layer
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl_.metalLayer nextDrawable];
        if (!drawable) {
            return;
        }

        id<MTLCommandBuffer> commandBuffer = [impl_.commandQueue commandBuffer];

        // Blit canvas texture to drawable - use actual texture size for proper Retina support
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

        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
}

int MetalStampRenderer::get_current_stroke_point_count() const {
    return is_ready() ? impl_.pointCount : 0;
}

int MetalStampRenderer::get_points_rendered() const {
    return is_ready() ? impl_.pointsRendered : 0;
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

} // extern "C"
