// Fiction Text Renderer - Vulkan-based text rendering for narrative games
// Uses stb_truetype for proper font rendering with UTF-8 support
//
// This provides Disco Elysium-style dialogue rendering:
// - Colored text per character/speaker
// - Scrolling dialogue history
// - Choice highlighting
// - Film-strip aesthetic panel

#pragma once

// Include fiction_engine.hpp first which has SDL/Vulkan setup
#include "fiction_engine.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <chrono>
#include <sys/stat.h>
#include <cstdio>  // For popen/pclose

// stb_truetype for proper font rendering
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// stb_image for background image loading (implementation in stb_impl.o)
// Disable thread_local in stb_image - macOS JIT can't resolve _tlv_bootstrap
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

namespace fiction {

// =============================================================================
// Font Glyph Data - using stb_truetype for proper Unicode support
// =============================================================================

struct GlyphInfo {
    float u0, v0, u1, v1;  // UV coordinates in atlas
    float width;           // Character width in pixels
    float height;          // Character height in pixels
    float advance;         // How far to move cursor
    float xoffset;         // X offset for rendering
    float yoffset;         // Y offset for rendering
};

struct FontAtlas {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    uint32_t atlasWidth = 512;   // Larger for Unicode glyphs
    uint32_t atlasHeight = 512;
    
    // Use int32_t for Unicode codepoints
    std::unordered_map<int32_t, GlyphInfo> glyphs;
    float lineHeight = 24.0f;
    float spaceWidth = 8.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float scale = 0.0f;
    
    // stb_truetype font info
    stbtt_fontinfo fontInfo;
    std::vector<uint8_t> fontData;  // Keep font data alive
};

// =============================================================================
// Text Vertex
// =============================================================================

struct TextVertex {
    float x, y;         // Position
    float u, v;         // Texture coordinates
    float r, g, b, a;   // Color
};

// =============================================================================
// Text Line - a segment of text with uniform color
// =============================================================================

struct TextSegment {
    std::string text;
    float r, g, b, a;   // Color
    bool bold = false;
    bool italic = false;
};

struct TextLine {
    std::vector<TextSegment> segments;
    float indent = 0.0f;
};

// =============================================================================
// Dialogue Entry Types
// =============================================================================

enum class EntryType {
    Dialogue,      // Character speech
    Narration,     // Narrator text
    Choice,        // Player choice (available)
    ChoiceSelected // Player choice (already selected - muted)
};

struct DialogueEntry {
    EntryType type;
    std::string speaker;      // Speaker name (empty for narration)
    std::string text;         // The actual text
    float speakerR, speakerG, speakerB;  // Speaker name color
    bool selected = false;    // For choices
};

// =============================================================================
// Panel Style — all visual config, settable from jank
// =============================================================================

struct PanelStyle {
    // Layout
    float panelX = 0.70f;         // Panel X position (fraction of screen)
    float panelWidth = 0.30f;     // Panel width (fraction of screen)
    float panelPadding = 20.0f;   // Inner padding in pixels
    float bottomMargin = 250.0f;  // Extra space at bottom (for ~10 lines of text)
    float lineSpacing = 4.0f;     // Extra spacing between lines
    float entrySpacing = 2.0f;    // lineSpacing multiplier between entries
    float choiceIndent = 20.0f;   // Indent for choice text

    // Scale
    float textScale = 1.0f;       // Base text scale
    float speakerScale = 1.25f;   // Speaker name scale (relative to textScale)

    // Panel background
    float bgR = 0.0f, bgG = 0.0f, bgB = 0.0f, bgA = 0.0f;

    // Text colors
    float textR = 0.8f, textG = 0.8f, textB = 0.8f;
    float narrationR = 0.75f, narrationG = 0.75f, narrationB = 0.78f;
    float choiceR = 0.85f, choiceG = 0.55f, choiceB = 0.25f;
    float choiceHoverR = 1.0f, choiceHoverG = 0.8f, choiceHoverB = 0.4f;
    float choiceSelectedR = 0.5f, choiceSelectedG = 0.5f, choiceSelectedB = 0.5f;
    float choiceSelectedHoverR = 0.7f, choiceSelectedHoverG = 0.7f, choiceSelectedHoverB = 0.7f;
};

// =============================================================================
// Text Renderer State
// =============================================================================

struct ChoiceBounds {
    float y0, y1;  // Top and bottom Y coordinates
    int index;     // Choice index (0-based)
};

struct TextRenderer {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    
    // Font
    FontAtlas font;
    
    // Text pipeline
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    // Vertex buffer (dynamic, rebuilt each frame)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void* vertexMapped = nullptr;
    uint32_t maxVertices = 65536;
    uint32_t vertexCount = 0;
    
    // Screen dimensions
    float screenWidth = 1280.0f;
    float screenHeight = 720.0f;
    
    // Visual style (all settable from jank)
    PanelStyle style;
    
    // Scroll state
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float targetScrollOffset = 0.0f;   // For auto-scroll animation
    float scrollAnimationSpeed = 16.0f;  // Lines per second (2x faster)
    bool autoScrollEnabled = true;      // Enable auto-scroll when new content added
    bool isAutoScrolling = false;       // Currently in auto-scroll animation
    int lastEntryCount = 0;             // Track when new entries are added
    
    // Mouse state
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    int hoveredChoice = -1;  // -1 = no hover, 0+ = choice index
    
    // Choice bounds (rebuilt each frame)
    std::vector<ChoiceBounds> choiceBounds;
    
    bool initialized = false;
};

// =============================================================================
// Speaker Name Particle Effect
// =============================================================================

struct SpeakerParticleQuad {
    float x, y, w, h;       // Position and size in pixels
    float r, g, b;           // Speaker color
};

struct ParticleRenderer {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    void* uniformMapped = nullptr;
    
    // Vertex buffer for particle quads (separate from text)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void* vertexMapped = nullptr;
    uint32_t maxVertices = 4096;
    uint32_t vertexCount = 0;
    
    // Collected speaker name positions for this frame
    std::vector<SpeakerParticleQuad> speakerQuads;
    
    // Time tracking for animation
    std::chrono::steady_clock::time_point startTimePoint;
    
    bool initialized = false;
};

// Use inline global for JIT compatibility
inline ParticleRenderer* g_particle_renderer = nullptr;

inline ParticleRenderer*& get_particle_renderer() {
    return g_particle_renderer;
}

// =============================================================================
// ODR-safe global accessor
// =============================================================================

inline TextRenderer*& get_text_renderer() {
    static TextRenderer* ptr = nullptr;
    return ptr;
}

// =============================================================================
// Background Renderer
// =============================================================================

struct BackgroundRenderer {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    
    // Vertex buffer for the fullscreen quad (6 vertices)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    
    int imgWidth = 0;
    int imgHeight = 0;
    
    bool initialized = false;
};

// Use cpp/raw-safe global for JIT compatibility (no static local TLV guards)
inline BackgroundRenderer* g_bg_renderer = nullptr;

inline BackgroundRenderer*& get_bg_renderer() {
    return g_bg_renderer;
}

// Forward declaration for SPIRV loading
inline std::vector<uint32_t> load_text_spirv(const std::string& path);

// Forward declarations for pipeline creation and hot reload
inline VkPipeline create_pipeline(VkDevice device,
                                   const std::string& shaderDir,
                                   const std::string& shaderName,
                                   VkPipelineLayout pipelineLayout,
                                   VkRenderPass renderPass,
                                   bool alphaBlend,
                                   const char* vertEntry = "main",
                                   const char* fragEntry = "main");
inline void watch_shader(const std::string& name, bool alphaBlend,
                          VkPipeline* pipeline, VkPipelineLayout* pipelineLayout,
                          const char* vertEntry = "main", const char* fragEntry = "main");

inline bool load_background_image(const char* filepath,
                                   VkDevice device,
                                   VkPhysicalDevice physicalDevice,
                                   VkQueue graphicsQueue,
                                   VkCommandPool commandPool,
                                   VkRenderPass renderPass,
                                   float screenWidth,
                                   float screenHeight,
                                   const std::string& shaderDir) {
    // Load image with stb_image
    int w, h, channels;
    unsigned char* pixels = stbi_load(filepath, &w, &h, &channels, 4); // Force RGBA
    if (!pixels) {
        std::cerr << "[fiction] Failed to load background: " << filepath << std::endl;
        return false;
    }
    
    std::cout << "[fiction] Background loaded: " << w << "x" << h << " (" << channels << " ch)" << std::endl;
    
    auto* bg = new BackgroundRenderer();
    get_bg_renderer() = bg;
    bg->imgWidth = w;
    bg->imgHeight = h;
    
    // Create Vulkan image (RGBA8)
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {(uint32_t)w, (uint32_t)h, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    vkCreateImage(device, &imageInfo, nullptr, &bg->image);
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, bg->image, &memReq);
    
    // Find device-local memory
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memTypeIdx = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIdx = i;
            break;
        }
    }
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;
    vkAllocateMemory(device, &allocInfo, nullptr, &bg->imageMemory);
    vkBindImageMemory(device, bg->image, bg->imageMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = bg->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &viewInfo, nullptr, &bg->imageView);
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(device, &samplerInfo, nullptr, &bg->sampler);
    
    // Upload pixel data via staging buffer
    VkDeviceSize imageSize = w * h * 4;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = imageSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuffer);
    
    VkMemoryRequirements stagingReq;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingReq);
    
    uint32_t stagingMemType = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((stagingReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            stagingMemType = i;
            break;
        }
    }
    
    VkMemoryAllocateInfo stagingAllocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    stagingAllocInfo.allocationSize = stagingReq.size;
    stagingAllocInfo.memoryTypeIndex = stagingMemType;
    vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);
    
    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, imageSize);
    vkUnmapMemory(device, stagingMemory);
    
    stbi_image_free(pixels);
    
    // Transfer image layout and copy
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cmdInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Transition to TRANSFER_DST
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = bg->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    
    vkCmdCopyBufferToImage(cmd, stagingBuffer, bg->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to SHADER_READ_ONLY
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &bg->descriptorSetLayout);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &bg->descriptorPool);
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAllocInfo.descriptorPool = bg->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &bg->descriptorSetLayout;
    vkAllocateDescriptorSets(device, &dsAllocInfo, &bg->descriptorSet);
    
    // Update descriptor set
    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.sampler = bg->sampler;
    descImageInfo.imageView = bg->imageView;
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bg->descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &descImageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    
    // Load background shaders and create pipeline
    // Push constant range
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 2;
    
    // Pipeline layout
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &bg->descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &plInfo, nullptr, &bg->pipelineLayout);
    
    // Use generic pipeline creator (no alpha blend for opaque background)
    bg->pipeline = create_pipeline(device, shaderDir, "bg",
                                    bg->pipelineLayout, renderPass, false);
    if (bg->pipeline == VK_NULL_HANDLE) {
        return false;
    }
    
    // Register bg pipeline for hot reload
    watch_shader("bg", false, &bg->pipeline, &bg->pipelineLayout);
    
    // Create vertex buffer for fullscreen quad (6 vertices)
    VkDeviceSize vbSize = 6 * sizeof(TextVertex);
    
    VkBufferCreateInfo vbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vbInfo.size = vbSize;
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &vbInfo, nullptr, &bg->vertexBuffer);
    
    VkMemoryRequirements vbMemReq;
    vkGetBufferMemoryRequirements(device, bg->vertexBuffer, &vbMemReq);
    
    uint32_t vbMemType = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((vbMemReq.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            vbMemType = i;
            break;
        }
    }
    
    VkMemoryAllocateInfo vbAllocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    vbAllocInfo.allocationSize = vbMemReq.size;
    vbAllocInfo.memoryTypeIndex = vbMemType;
    vkAllocateMemory(device, &vbAllocInfo, nullptr, &bg->vertexMemory);
    vkBindBufferMemory(device, bg->vertexBuffer, bg->vertexMemory, 0);
    
    // Fill vertex data - fullscreen quad covering entire screen
    void* vbData;
    vkMapMemory(device, bg->vertexMemory, 0, vbSize, 0, &vbData);
    TextVertex* verts = (TextVertex*)vbData;
    
    float sw = screenWidth;
    float sh = screenHeight;
    
    // Two triangles covering the full screen, UV mapped to full texture
    // Triangle 1: top-left, top-right, bottom-left
    verts[0] = {0,  0,  0, 0, 1, 1, 1, 1};
    verts[1] = {sw, 0,  1, 0, 1, 1, 1, 1};
    verts[2] = {0,  sh, 0, 1, 1, 1, 1, 1};
    // Triangle 2: top-right, bottom-right, bottom-left
    verts[3] = {sw, 0,  1, 0, 1, 1, 1, 1};
    verts[4] = {sw, sh, 1, 1, 1, 1, 1, 1};
    verts[5] = {0,  sh, 0, 1, 1, 1, 1, 1};
    
    vkUnmapMemory(device, bg->vertexMemory);
    
    bg->initialized = true;
    std::cout << "[fiction] Background renderer initialized" << std::endl;
    return true;
}

inline void record_bg_commands(VkCommandBuffer cmd) {
    auto* bg = get_bg_renderer();
    auto* tr = get_text_renderer();
    if (!bg || !bg->initialized || !tr) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bg->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bg->pipelineLayout,
                            0, 1, &bg->descriptorSet, 0, nullptr);
    
    float screenSize[2] = {tr->screenWidth, tr->screenHeight};
    vkCmdPushConstants(cmd, bg->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(screenSize), screenSize);
    
    VkViewport viewport{};
    viewport.width = tr->screenWidth;
    viewport.height = tr->screenHeight;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.extent = {(uint32_t)tr->screenWidth, (uint32_t)tr->screenHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &bg->vertexBuffer, &offset);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

// Simple init that gets Vulkan handles from fiction_engine
inline bool load_background_image_simple(const char* filepath,
                                          const std::string& shaderDir = "vulkan_fiction") {
    auto* tr = get_text_renderer();
    if (!tr) {
        std::cerr << "[fiction] Text renderer must be initialized before background" << std::endl;
        return false;
    }
    return load_background_image(
        filepath,
        fiction_engine::get_device(),
        fiction_engine::get_physical_device(),
        fiction_engine::get_graphics_queue(),
        fiction_engine::get_command_pool(),
        fiction_engine::get_render_pass(),
        tr->screenWidth,
        tr->screenHeight,
        shaderDir
    );
}

inline void cleanup_bg_renderer() {
    auto* bg = get_bg_renderer();
    if (!bg) return;
    
    auto device = fiction_engine::get_device();
    if (bg->vertexBuffer) { vkDestroyBuffer(device, bg->vertexBuffer, nullptr); }
    if (bg->vertexMemory) { vkFreeMemory(device, bg->vertexMemory, nullptr); }
    if (bg->pipeline) { vkDestroyPipeline(device, bg->pipeline, nullptr); }
    if (bg->pipelineLayout) { vkDestroyPipelineLayout(device, bg->pipelineLayout, nullptr); }
    if (bg->descriptorPool) { vkDestroyDescriptorPool(device, bg->descriptorPool, nullptr); }
    if (bg->descriptorSetLayout) { vkDestroyDescriptorSetLayout(device, bg->descriptorSetLayout, nullptr); }
    if (bg->sampler) { vkDestroySampler(device, bg->sampler, nullptr); }
    if (bg->imageView) { vkDestroyImageView(device, bg->imageView, nullptr); }
    if (bg->image) { vkDestroyImage(device, bg->image, nullptr); }
    if (bg->imageMemory) { vkFreeMemory(device, bg->imageMemory, nullptr); }
    
    delete bg;
    get_bg_renderer() = nullptr;
}

// =============================================================================
// Particle Renderer Init / Cleanup
// =============================================================================

inline void cleanup_particle_renderer();

inline bool init_particle_renderer(VkDevice device,
                                    VkPhysicalDevice physicalDevice,
                                    VkRenderPass renderPass,
                                    const std::string& shaderDir) {
    // Check shader files exist first
    auto vertSpirv = load_text_spirv(shaderDir + "/particle.vert.spv");
    auto fragSpirv = load_text_spirv(shaderDir + "/particle.frag.spv");
    
    if (vertSpirv.empty() || fragSpirv.empty()) {
        std::cerr << "[fiction] Particle shaders not found in " << shaderDir
                  << ", skipping particle renderer" << std::endl;
        return false;
    }
    
    auto* pr = new ParticleRenderer();
    get_particle_renderer() = pr;
    
    // Record start time for animation
    pr->startTimePoint = std::chrono::steady_clock::now();

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    auto find_host_visible_mem = [&](uint32_t typeBits) -> uint32_t {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                return i;
            }
        }
        return 0;
    };

    // Uniform descriptor set for shared WGSL shader uniforms.
    VkDescriptorSetLayoutBinding uniformBinding{};
    uniformBinding.binding = 0;
    uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBinding.descriptorCount = 1;
    uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &uniformBinding;
    vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &pr->descriptorSetLayout);

    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &pr->descriptorSetLayout;
    plInfo.pushConstantRangeCount = 0;
    plInfo.pPushConstantRanges = nullptr;
    vkCreatePipelineLayout(device, &plInfo, nullptr, &pr->pipelineLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(device, &dpInfo, nullptr, &pr->descriptorPool);

    VkDescriptorSetAllocateInfo dsAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAlloc.descriptorPool = pr->descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &pr->descriptorSetLayout;
    vkAllocateDescriptorSets(device, &dsAlloc, &pr->descriptorSet);

    VkBufferCreateInfo ubInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ubInfo.size = sizeof(float) * 4;  // vec2 screenSize + time + yFlip
    ubInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ubInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &ubInfo, nullptr, &pr->uniformBuffer);

    VkMemoryRequirements ubMemReq;
    vkGetBufferMemoryRequirements(device, pr->uniformBuffer, &ubMemReq);
    VkMemoryAllocateInfo ubAllocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ubAllocInfo.allocationSize = ubMemReq.size;
    ubAllocInfo.memoryTypeIndex = find_host_visible_mem(ubMemReq.memoryTypeBits);
    vkAllocateMemory(device, &ubAllocInfo, nullptr, &pr->uniformMemory);
    vkBindBufferMemory(device, pr->uniformBuffer, pr->uniformMemory, 0);
    vkMapMemory(device, pr->uniformMemory, 0, ubInfo.size, 0, &pr->uniformMapped);

    VkDescriptorBufferInfo ubDesc{};
    ubDesc.buffer = pr->uniformBuffer;
    ubDesc.offset = 0;
    ubDesc.range = ubInfo.size;
    VkWriteDescriptorSet ubWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ubWrite.dstSet = pr->descriptorSet;
    ubWrite.dstBinding = 0;
    ubWrite.descriptorCount = 1;
    ubWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubWrite.pBufferInfo = &ubDesc;
    vkUpdateDescriptorSets(device, 1, &ubWrite, 0, nullptr);

    // Use generic pipeline creator (alpha blend for particles)
    pr->pipeline = create_pipeline(device, shaderDir, "particle",
                                    pr->pipelineLayout, renderPass, true,
                                    "vs_main", "fs_main");
    if (pr->pipeline == VK_NULL_HANDLE) {
        cleanup_particle_renderer();
        return false;
    }

    // Create vertex buffer (host-visible, persistently mapped)
    VkDeviceSize vbSize = pr->maxVertices * sizeof(TextVertex);
    VkBufferCreateInfo vbInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vbInfo.size = vbSize;
    vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &vbInfo, nullptr, &pr->vertexBuffer);

    VkMemoryRequirements vbMemReq;
    vkGetBufferMemoryRequirements(device, pr->vertexBuffer, &vbMemReq);
    VkMemoryAllocateInfo vbAllocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    vbAllocInfo.allocationSize = vbMemReq.size;
    vbAllocInfo.memoryTypeIndex = find_host_visible_mem(vbMemReq.memoryTypeBits);
    vkAllocateMemory(device, &vbAllocInfo, nullptr, &pr->vertexMemory);
    vkBindBufferMemory(device, pr->vertexBuffer, pr->vertexMemory, 0);
    vkMapMemory(device, pr->vertexMemory, 0, vbSize, 0, &pr->vertexMapped);
    
    pr->initialized = true;
    std::cout << "[fiction] Particle renderer initialized" << std::endl;
    return true;
}

inline void cleanup_particle_renderer() {
    auto* pr = get_particle_renderer();
    if (!pr) return;
    
    auto device = fiction_engine::get_device();
    if (pr->vertexBuffer) {
        vkUnmapMemory(device, pr->vertexMemory);
        vkDestroyBuffer(device, pr->vertexBuffer, nullptr);
        vkFreeMemory(device, pr->vertexMemory, nullptr);
    }
    if (pr->uniformBuffer) {
        vkUnmapMemory(device, pr->uniformMemory);
        vkDestroyBuffer(device, pr->uniformBuffer, nullptr);
        vkFreeMemory(device, pr->uniformMemory, nullptr);
    }
    if (pr->pipeline) vkDestroyPipeline(device, pr->pipeline, nullptr);
    if (pr->pipelineLayout) vkDestroyPipelineLayout(device, pr->pipelineLayout, nullptr);
    if (pr->descriptorPool) vkDestroyDescriptorPool(device, pr->descriptorPool, nullptr);
    if (pr->descriptorSetLayout) vkDestroyDescriptorSetLayout(device, pr->descriptorSetLayout, nullptr);
    
    delete pr;
    get_particle_renderer() = nullptr;
}

// Forward declaration
inline void cleanup_text_renderer();

// =============================================================================
// Helper: Find memory type
// =============================================================================

inline uint32_t find_text_memory_type(TextRenderer* tr, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(tr->physicalDevice, &memProps);
    
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

// =============================================================================
// Font Atlas Creation with stb_truetype
// =============================================================================

// Default font path - can be overridden
inline std::string& get_font_path() {
    static std::string path = "fonts/CrimsonPro-Regular.ttf";
    return path;
}

inline void set_font_path(const std::string& path) {
    get_font_path() = path;
}

// French accented characters we need to support
inline std::vector<int32_t> get_required_codepoints() {
    std::vector<int32_t> codepoints;
    
    // Basic ASCII (32-126)
    for (int c = 32; c < 127; c++) {
        codepoints.push_back(c);
    }
    
    // French accented characters
    // À Â Ä Ç È É Ê Ë Î Ï Ô Ö Ù Û Ü Ÿ
    // à â ä ç è é ê ë î ï ô ö ù û ü ÿ
    // Œ œ « » – — ' ' " "
    int32_t french[] = {
        0x00C0, 0x00C2, 0x00C4, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB,  // À Â Ä Ç È É Ê Ë
        0x00CE, 0x00CF, 0x00D4, 0x00D6, 0x00D9, 0x00DB, 0x00DC, 0x0178,  // Î Ï Ô Ö Ù Û Ü Ÿ
        0x00E0, 0x00E2, 0x00E4, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB,  // à â ä ç è é ê ë
        0x00EE, 0x00EF, 0x00F4, 0x00F6, 0x00F9, 0x00FB, 0x00FC, 0x00FF,  // î ï ô ö ù û ü ÿ
        0x0152, 0x0153,  // Œ œ
        0x00AB, 0x00BB,  // « »
        0x2013, 0x2014,  // – —
        0x2018, 0x2019, 0x201C, 0x201D,  // ' ' " "
        0x2026,  // …
        0x00A0,  //   (NO-BREAK SPACE - used with French guillemets)
        0x0394,  // Δ (Delta - used in story format #∆)
    };
    
    for (int32_t cp : french) {
        codepoints.push_back(cp);
    }
    
    return codepoints;
}

inline void create_simple_font_atlas(TextRenderer* tr) {
    // Load TTF font file
    std::ifstream fontFile(get_font_path(), std::ios::binary | std::ios::ate);
    if (!fontFile.is_open()) {
        std::cerr << "[fiction] Failed to open font: " << get_font_path() << std::endl;
        // Fall back to creating a minimal atlas
        tr->font.atlasWidth = 512;
        tr->font.atlasHeight = 512;
        goto create_vulkan_resources;
    }
    
    {
        size_t fontFileSize = fontFile.tellg();
        fontFile.seekg(0);
        tr->font.fontData.resize(fontFileSize);
        fontFile.read(reinterpret_cast<char*>(tr->font.fontData.data()), fontFileSize);
        fontFile.close();
        
        // Initialize stb_truetype
        if (!stbtt_InitFont(&tr->font.fontInfo, tr->font.fontData.data(), 0)) {
            std::cerr << "[fiction] Failed to init font" << std::endl;
            goto create_vulkan_resources;
        }
        
        // Font size in pixels
        float fontSize = 32.0f;
        tr->font.scale = stbtt_ScaleForPixelHeight(&tr->font.fontInfo, fontSize);
        
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&tr->font.fontInfo, &ascent, &descent, &lineGap);
        tr->font.ascent = ascent * tr->font.scale;
        tr->font.descent = descent * tr->font.scale;
        tr->font.lineHeight = (ascent - descent + lineGap) * tr->font.scale;
        
        // Get space width
        int spaceAdvance, spaceLsb;
        stbtt_GetCodepointHMetrics(&tr->font.fontInfo, ' ', &spaceAdvance, &spaceLsb);
        tr->font.spaceWidth = spaceAdvance * tr->font.scale;
    }
    
    tr->font.atlasWidth = 512;
    tr->font.atlasHeight = 512;
    
create_vulkan_resources:
    // Create Vulkan image
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8_UNORM;  // Single channel for font
    imageInfo.extent = {tr->font.atlasWidth, tr->font.atlasHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    vkCreateImage(tr->device, &imageInfo, nullptr, &tr->font.image);
    
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(tr->device, tr->font.image, &memReq);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_text_memory_type(tr, memReq.memoryTypeBits, 
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    vkAllocateMemory(tr->device, &allocInfo, nullptr, &tr->font.memory);
    vkBindImageMemory(tr->device, tr->font.image, tr->font.memory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = tr->font.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    vkCreateImageView(tr->device, &viewInfo, nullptr, &tr->font.imageView);
    
    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    
    vkCreateSampler(tr->device, &samplerInfo, nullptr, &tr->font.sampler);
}

// =============================================================================
// Vertex Buffer Creation
// =============================================================================

inline void create_text_vertex_buffer(TextRenderer* tr) {
    VkDeviceSize bufferSize = tr->maxVertices * sizeof(TextVertex);
    
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    vkCreateBuffer(tr->device, &bufferInfo, nullptr, &tr->vertexBuffer);
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(tr->device, tr->vertexBuffer, &memReq);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_text_memory_type(tr, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    vkAllocateMemory(tr->device, &allocInfo, nullptr, &tr->vertexMemory);
    vkBindBufferMemory(tr->device, tr->vertexBuffer, tr->vertexMemory, 0);
    vkMapMemory(tr->device, tr->vertexMemory, 0, bufferSize, 0, &tr->vertexMapped);
}

// =============================================================================
// Text Rendering - Build Vertices
// =============================================================================

inline void add_text_quad(TextRenderer* tr, 
                          float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a) {
    if (tr->vertexCount + 6 > tr->maxVertices) return;
    
    TextVertex* verts = (TextVertex*)tr->vertexMapped + tr->vertexCount;
    
    // Triangle 1
    verts[0] = {x,     y,     u0, v0, r, g, b, a};
    verts[1] = {x + w, y,     u1, v0, r, g, b, a};
    verts[2] = {x,     y + h, u0, v1, r, g, b, a};
    
    // Triangle 2
    verts[3] = {x + w, y,     u1, v0, r, g, b, a};
    verts[4] = {x + w, y + h, u1, v1, r, g, b, a};
    verts[5] = {x,     y + h, u0, v1, r, g, b, a};
    
    tr->vertexCount += 6;
}

// Add a colored rectangle (for backgrounds, panels)
inline void add_rect(TextRenderer* tr,
                     float x, float y, float w, float h,
                     float r, float g, float b, float a) {
    // Use a solid white area of the font atlas (or separate solid texture)
    // For now, use UV 0,0 which should be a corner pixel
    add_text_quad(tr, x, y, w, h, 0.0f, 0.0f, 0.01f, 0.01f, r, g, b, a);
}

// =============================================================================
// UTF-8 Decoding
// =============================================================================

// Decode one UTF-8 codepoint from string, advance index
inline int32_t decode_utf8(const std::string& str, size_t& i) {
    if (i >= str.size()) return 0;
    
    uint8_t c = str[i];
    
    // ASCII (0xxxxxxx)
    if ((c & 0x80) == 0) {
        i++;
        return c;
    }
    
    // 2-byte sequence (110xxxxx 10xxxxxx)
    if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
        int32_t cp = (c & 0x1F) << 6;
        cp |= (str[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    
    // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
    if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
        int32_t cp = (c & 0x0F) << 12;
        cp |= (str[i + 1] & 0x3F) << 6;
        cp |= (str[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    
    // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
    if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
        int32_t cp = (c & 0x07) << 18;
        cp |= (str[i + 1] & 0x3F) << 12;
        cp |= (str[i + 2] & 0x3F) << 6;
        cp |= (str[i + 3] & 0x3F);
        i += 4;
        return cp;
    }
    
    // Invalid, skip byte
    i++;
    return 0xFFFD;  // Replacement character
}

// Render a string at position, returns ending X position
inline float render_text_string(TextRenderer* tr,
                                const std::string& text,
                                float x, float y,
                                float scale,
                                float r, float g, float b, float a) {
    float cursorX = x;
    
    size_t i = 0;
    while (i < text.size()) {
        int32_t cp = decode_utf8(text, i);
        
        if (cp == ' ') {
            cursorX += tr->font.spaceWidth * scale;
            continue;
        }
        
        auto it = tr->font.glyphs.find(cp);
        if (it == tr->font.glyphs.end()) {
            // Try fallback to '?' for unknown chars
            it = tr->font.glyphs.find('?');
            if (it == tr->font.glyphs.end()) {
                cursorX += tr->font.spaceWidth * scale;
                continue;
            }
        }
        
        const GlyphInfo& gi = it->second;
        float charW = gi.width * scale;
        float charH = gi.height * scale;
        
        add_text_quad(tr,
                      cursorX + gi.xoffset * scale,
                      y + gi.yoffset * scale,
                      charW, charH,
                      gi.u0, gi.v0, gi.u1, gi.v1,
                      r, g, b, a);
        
        cursorX += gi.advance * scale;
    }
    
    return cursorX;
}

// Measure width of a UTF-8 string
inline float measure_text_width(TextRenderer* tr, const std::string& text, float scale) {
    float width = 0.0f;
    size_t i = 0;
    while (i < text.size()) {
        int32_t cp = decode_utf8(text, i);
        if (cp == ' ') {
            width += tr->font.spaceWidth * scale;
        } else {
            auto it = tr->font.glyphs.find(cp);
            if (it != tr->font.glyphs.end()) {
                width += it->second.advance * scale;
            } else {
                width += tr->font.spaceWidth * scale;
            }
        }
    }
    return width;
}

// Word-wrap text and return lines (UTF-8 aware)
inline std::vector<std::string> wrap_text(TextRenderer* tr, 
                                          const std::string& text,
                                          float maxWidth,
                                          float scale) {
    std::vector<std::string> lines;
    std::string currentLine;
    float currentWidth = 0.0f;
    
    size_t i = 0;
    while (i < text.size()) {
        // Find next word (UTF-8 aware)
        size_t wordStart = i;
        while (i < text.size()) {
            size_t save_i = i;
            int32_t cp = decode_utf8(text, i);
            if (cp == ' ' || cp == '\n') {
                i = save_i;  // Don't consume the space/newline
                break;
            }
        }
        std::string word = text.substr(wordStart, i - wordStart);
        
        // Measure word width
        float wordWidth = measure_text_width(tr, word, scale);
        
        // Check if word fits on current line
        if (currentWidth + wordWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine = word;
            currentWidth = wordWidth;
        } else {
            if (!currentLine.empty()) {
                currentLine += " ";
                currentWidth += tr->font.spaceWidth * scale;
            }
            currentLine += word;
            currentWidth += wordWidth;
        }
        
        // Handle space or newline after word
        if (i < text.size()) {
            size_t save_i = i;
            int32_t cp = decode_utf8(text, i);
            if (cp == '\n') {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                }
                currentLine.clear();
                currentWidth = 0.0f;
            }
            // Space is consumed but not added to line
        }
    }
    
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

// =============================================================================
// Render Dialogue Panel
// =============================================================================

inline float render_dialogue_entry(TextRenderer* tr,
                                   const DialogueEntry& entry,
                                   float y,
                                   float scale,
                                   bool isHovered = false) {
    float panelStartX = tr->screenWidth * tr->style.panelX + tr->style.panelPadding;
    float textWidth = tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2;
    float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;
    
    float currentY = y;
    
    // Colors based on entry type (from config)
    float textR = tr->style.textR, textG = tr->style.textG, textB = tr->style.textB;
    
    switch (entry.type) {
        case EntryType::Choice:
            if (isHovered) {
                textR = tr->style.choiceHoverR; textG = tr->style.choiceHoverG; textB = tr->style.choiceHoverB;
            } else {
                textR = tr->style.choiceR; textG = tr->style.choiceG; textB = tr->style.choiceB;
            }
            break;
        case EntryType::ChoiceSelected:
            if (isHovered) {
                textR = tr->style.choiceSelectedHoverR; textG = tr->style.choiceSelectedHoverG; textB = tr->style.choiceSelectedHoverB;
            } else {
                textR = tr->style.choiceSelectedR; textG = tr->style.choiceSelectedG; textB = tr->style.choiceSelectedB;
            }
            break;
        case EntryType::Narration:
            textR = tr->style.narrationR; textG = tr->style.narrationG; textB = tr->style.narrationB;
            break;
        default:
            break;
    }
    
    // Render speaker name if present (ALL CAPS, larger scale)
    if (!entry.speaker.empty()) {
        float speakerS = scale * tr->style.speakerScale;
        float speakerLineH = tr->font.lineHeight * speakerS + tr->style.lineSpacing;
        std::string upperSpeaker = entry.speaker;
        for (auto& c : upperSpeaker) c = std::toupper(static_cast<unsigned char>(c));
        float speakerEndX = render_text_string(tr, upperSpeaker, panelStartX, currentY, speakerS,
                          entry.speakerR, entry.speakerG, entry.speakerB, 1.0f);
        
        // Small painted square to the left of speaker name
        auto* pr = get_particle_renderer();
        if (pr && pr->initialized) {
            float size = speakerLineH * 0.40f;
            float gap = 8.0f;
            SpeakerParticleQuad spq;
            spq.x = panelStartX - gap - size;
            spq.y = currentY + (speakerLineH - size) * 0.5f;
            spq.w = size;
            spq.h = size;
            spq.r = entry.speakerR;
            spq.g = entry.speakerG;
            spq.b = entry.speakerB;
            pr->speakerQuads.push_back(spq);
        }
        
        currentY += speakerLineH;
    }
    
    // Word-wrap and render the text
    auto lines = wrap_text(tr, entry.text, textWidth, scale);
    for (const auto& line : lines) {
        // Add indent for choices
        float indent = (entry.type == EntryType::Choice || 
                        entry.type == EntryType::ChoiceSelected) ? tr->style.choiceIndent : 0.0f;
        
        render_text_string(tr, line, panelStartX + indent, currentY, scale,
                          textR, textG, textB, 1.0f);
        currentY += lineH;
    }
    
    // Add spacing after entry
    currentY += tr->style.lineSpacing * 2;
    
    return currentY;
}

inline void render_dialogue_panel(TextRenderer* tr,
                                  const std::vector<DialogueEntry>& history,
                                  const std::vector<DialogueEntry>& choices) {
    tr->vertexCount = 0;  // Reset vertices
    tr->choiceBounds.clear();  // Reset choice bounds

    // Reset particle data every frame so quads always follow current text layout.
    auto* pr = get_particle_renderer();
    if (pr && pr->initialized) {
        pr->speakerQuads.clear();
        pr->vertexCount = 0;
    }
    
    float panelX = tr->screenWidth * tr->style.panelX;
    float panelW = tr->screenWidth * tr->style.panelWidth;
    float panelH = tr->screenHeight;
    
    // Draw panel background
    add_rect(tr, panelX, 0, panelW, panelH,
             tr->style.bgR, tr->style.bgG, tr->style.bgB, tr->style.bgA);
    
    // Film strip edge decoration (right side)
    float stripWidth = 15.0f;
    add_rect(tr, tr->screenWidth - stripWidth, 0, stripWidth, panelH,
             0.05f, 0.05f, 0.05f, 1.0f);
    
    // Render dialogue history
    float scale = tr->style.textScale;
    float y = tr->style.panelPadding - tr->scrollOffset;
    
    for (const auto& entry : history) {
        if (y > tr->screenHeight) break;  // Off-screen below
        
        float entryHeight = 0.0f;
        // Only render if visible
        if (y + 200 > 0) {  // Rough visibility check
            y = render_dialogue_entry(tr, entry, y, scale, false);
        } else {
            // Skip but account for height
            auto lines = wrap_text(tr, entry.text, 
                                   tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2,
                                   scale);
            float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;
            y += (!entry.speaker.empty() ? lineH : 0);
            y += lines.size() * lineH;
            y += tr->style.lineSpacing * 2;
        }
        // Extra gap between dialogue entries
        y += tr->style.lineSpacing * tr->style.entrySpacing;
    }
    
    // Separator before choices
    if (!choices.empty()) {
        float sepY = y + 10;
        add_rect(tr, panelX + tr->style.panelPadding, sepY, 
                 panelW - tr->style.panelPadding * 2, 1.0f,
                 0.4f, 0.4f, 0.4f, 0.5f);
        y = sepY + 20;
    }
    
    // Determine which choice is hovered based on mouse position
    tr->hoveredChoice = -1;
    float textWidth = tr->screenWidth * tr->style.panelWidth - tr->style.panelPadding * 2;
    float lineH = tr->font.lineHeight * scale + tr->style.lineSpacing;
    
    // Pre-calculate choice positions to determine hover
    float choiceY = y;
    for (size_t i = 0; i < choices.size(); i++) {
        std::string numberedText = std::to_string(i + 1) + ". " + choices[i].text;
        auto lines = wrap_text(tr, numberedText, textWidth, scale);
        float entryHeight = lines.size() * lineH + tr->style.lineSpacing * 2;
        
        // Check if mouse is within this choice's bounds
        if (tr->mouseX >= panelX && tr->mouseX <= panelX + panelW &&
            tr->mouseY >= choiceY && tr->mouseY < choiceY + entryHeight) {
            tr->hoveredChoice = static_cast<int>(i);
        }
        
        // Store bounds for click detection
        ChoiceBounds bounds;
        bounds.y0 = choiceY;
        bounds.y1 = choiceY + entryHeight;
        bounds.index = static_cast<int>(i);
        tr->choiceBounds.push_back(bounds);
        
        choiceY += entryHeight;
    }
    
    // Render choices with hover highlighting
    int choiceNum = 1;
    for (size_t i = 0; i < choices.size(); i++) {
        // Add number prefix
        DialogueEntry numberedChoice = choices[i];
        numberedChoice.text = std::to_string(choiceNum) + ". " + choices[i].text;
        bool isHovered = (tr->hoveredChoice == static_cast<int>(i));
        y = render_dialogue_entry(tr, numberedChoice, y, scale, isHovered);
        choiceNum++;
    }
    
    // Update max scroll (leave room at bottom for reading)
    tr->maxScroll = std::max(0.0f, y + tr->scrollOffset - tr->screenHeight + tr->style.bottomMargin);
    
    // Check if new entries were added
    int currentEntryCount = history.size() + choices.size();
    bool newContentAdded = (currentEntryCount > tr->lastEntryCount);
    tr->lastEntryCount = currentEntryCount;
    
    // Auto-scroll animation: only when new content added
    if (tr->autoScrollEnabled && tr->maxScroll > 0 && newContentAdded) {
        tr->isAutoScrolling = true;
        tr->targetScrollOffset = tr->maxScroll;
    }
    
    // Perform auto-scroll animation
    if (tr->isAutoScrolling) {
        float diff = tr->targetScrollOffset - tr->scrollOffset;
        if (std::abs(diff) > 0.5f) {
            float dt = 1.0f / 60.0f;
            float step = tr->scrollAnimationSpeed * tr->font.lineHeight * tr->style.textScale * dt;
            if (diff > 0) {
                tr->scrollOffset += std::min(diff, step);
            } else {
                tr->scrollOffset -= std::min(std::abs(diff), step);
            }
        } else {
            // Animation complete - let user scroll freely
            tr->scrollOffset = tr->targetScrollOffset;
            tr->isAutoScrolling = false;
        }
    }
    
    // Generate particle quad vertices from collected speaker positions
    if (pr && pr->initialized && !pr->speakerQuads.empty()) {
        pr->vertexCount = 0;
        TextVertex* pverts = (TextVertex*)pr->vertexMapped;
        
        for (const auto& spq : pr->speakerQuads) {
            if (pr->vertexCount + 6 > pr->maxVertices) break;
            
            // Two triangles covering the speaker name quad
            // UV: 0-1 across the quad, color = speaker RGB
            float x0 = spq.x, y0 = spq.y;
            float x1 = spq.x + spq.w, y1 = spq.y + spq.h;
            float r = spq.r, g = spq.g, b = spq.b, a = 1.0f;
            
            // Triangle 1: top-left, top-right, bottom-left
            pverts[pr->vertexCount + 0] = {x0, y0, 0.0f, 0.0f, r, g, b, a};
            pverts[pr->vertexCount + 1] = {x1, y0, 1.0f, 0.0f, r, g, b, a};
            pverts[pr->vertexCount + 2] = {x0, y1, 0.0f, 1.0f, r, g, b, a};
            // Triangle 2: top-right, bottom-right, bottom-left
            pverts[pr->vertexCount + 3] = {x1, y0, 1.0f, 0.0f, r, g, b, a};
            pverts[pr->vertexCount + 4] = {x1, y1, 1.0f, 1.0f, r, g, b, a};
            pverts[pr->vertexCount + 5] = {x0, y1, 0.0f, 1.0f, r, g, b, a};
            
            pr->vertexCount += 6;
        }
    }
}

// =============================================================================
// Shader loading helper (loads pre-compiled SPIR-V)
// =============================================================================

inline std::vector<uint32_t> load_text_spirv(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[fiction] Failed to open SPIR-V: " << path << std::endl;
        return {};
    }
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint32_t> spirv(size / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), size);
    return spirv;
}

// =============================================================================
// Shader Hot Reload System
// =============================================================================
// Polls source file modification times each frame. When a shader changes,
// recreates the affected pipeline on the fly. Works for bg/text GLSL shaders
// and particle WGSL shader. Run `make build-fiction-shaders` once up front;
// hot reload then recompiles changed sources on demand.

// Generic pipeline creation: one function for all pipelines.
// Only the things that differ are parameterized:
//   - shader name (vert/frag .spv prefix)
//   - pipeline layout (already created, owns push constants + descriptor sets)
//   - render pass
//   - alpha blending on/off
inline VkPipeline create_pipeline(VkDevice device,
                                   const std::string& shaderDir,
                                   const std::string& shaderName,
                                   VkPipelineLayout pipelineLayout,
                                   VkRenderPass renderPass,
                                   bool alphaBlend,
                                   const char* vertEntry,
                                   const char* fragEntry) {
    auto vertSpirv = load_text_spirv(shaderDir + "/" + shaderName + ".vert.spv");
    auto fragSpirv = load_text_spirv(shaderDir + "/" + shaderName + ".frag.spv");
    if (vertSpirv.empty() || fragSpirv.empty()) {
        std::cerr << "[fiction] Failed to load " << shaderName << " shaders" << std::endl;
        return VK_NULL_HANDLE;
    }
    
    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo vertModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vertModInfo.codeSize = vertSpirv.size() * sizeof(uint32_t);
    vertModInfo.pCode = vertSpirv.data();
    vkCreateShaderModule(device, &vertModInfo, nullptr, &vertModule);
    
    VkShaderModuleCreateInfo fragModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fragModInfo.codeSize = fragSpirv.size() * sizeof(uint32_t);
    fragModInfo.pCode = fragSpirv.data();
    vkCreateShaderModule(device, &fragModInfo, nullptr, &fragModule);
    
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = vertEntry;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = fragEntry;
    
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(TextVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attrDescs[3] = {};
    attrDescs[0].location = 0; attrDescs[0].format = VK_FORMAT_R32G32_SFLOAT; attrDescs[0].offset = offsetof(TextVertex, x);
    attrDescs[1].location = 1; attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT; attrDescs[1].offset = offsetof(TextVertex, u);
    attrDescs[2].location = 2; attrDescs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrDescs[2].offset = offsetof(TextVertex, r);
    
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrDescs;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1; viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_FALSE; depthStencil.depthWriteEnable = VK_FALSE;
    
    VkPipelineColorBlendAttachmentState blendAttachment{};
    if (alphaBlend) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        blendAttachment.blendEnable = VK_FALSE;
    }
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1; colorBlending.pAttachments = &blendAttachment;
    
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2; dynamicState.pDynamicStates = dynamicStates;
    
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2; pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    
    if (result != VK_SUCCESS) {
        std::cerr << "[fiction] Failed to create " << shaderName << " pipeline" << std::endl;
        return VK_NULL_HANDLE;
    }
    
    std::cout << "[fiction] Pipeline '" << shaderName << "' created OK" << std::endl;
    return pipeline;
}

// Swap an old pipeline for a newly created one. Returns true on success.
inline bool hot_reload_pipeline(VkDevice device,
                                 const std::string& shaderDir,
                                 const std::string& shaderName,
                                 VkPipelineLayout pipelineLayout,
                                 VkRenderPass renderPass,
                                 bool alphaBlend,
                                 const char* vertEntry,
                                 const char* fragEntry,
                                 VkPipeline& outPipeline) {
    VkPipeline newPipeline = create_pipeline(device, shaderDir, shaderName,
                                              pipelineLayout, renderPass, alphaBlend,
                                              vertEntry, fragEntry);
    if (newPipeline == VK_NULL_HANDLE) return false;
    
    if (outPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, outPipeline, nullptr);
    }
    outPipeline = newPipeline;
    return true;
}

// =============================================================================
// Shader File Watching
// =============================================================================

struct ShaderWatch {
    std::string name;           // e.g. "bg", "text", "particle"
    bool usesWgsl = false;      // true when shader is sourced from .wgsl
    std::string wgslPath;
    time_t wgslModTime = 0;
    time_t vertModTime = 0;
    time_t fragModTime = 0;
    bool alphaBlend;            // pipeline blend mode
    const char* vertEntry = "main";
    const char* fragEntry = "main";
    // Pointers to the pipeline and layout to hot-swap
    VkPipeline* pipeline = nullptr;
    VkPipelineLayout* pipelineLayout = nullptr;
};

struct ShaderHotReload {
    std::vector<ShaderWatch> watches;
    std::string shaderDir;
    float pollInterval = 0.5f;
    std::chrono::steady_clock::time_point lastPollTime;
    bool initialized = false;
};

inline ShaderHotReload* g_shader_hot_reload = nullptr;

inline ShaderHotReload*& get_shader_hot_reload() {
    return g_shader_hot_reload;
}

inline time_t get_file_mod_time_shader(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_mtime;
    }
    return 0;
}

inline std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

inline bool run_command_capture(const std::string& cmd, std::string& output) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return false;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    return pclose(pipe) == 0;
}

// Compile a shader source file to SPIR-V using glslc
inline bool compile_shader_source(const std::string& srcPath, const std::string& outPath, const std::string& stage) {
    std::string cmd = "glslc -fshader-stage=" + stage + " "
                    + shell_quote(srcPath) + " -o " + shell_quote(outPath) + " 2>&1";
    std::cout << "[fiction] Compiling shader: " << srcPath << " -> " << outPath << std::endl;

    std::string output;
    if (!run_command_capture(cmd, output)) {
        std::cerr << "[fiction] ERROR: Shader compilation failed for " << srcPath << std::endl;
        if (!output.empty()) {
            std::cerr << "[fiction] Compiler output:\n" << output << std::endl;
        }
        return false;
    }

    if (!output.empty()) {
        std::cout << "[fiction] Compiler output:\n" << output << std::endl;
    }

    std::cout << "[fiction] Successfully compiled: " << outPath << std::endl;
    return true;
}

inline bool compile_wgsl_particle(const std::string& wgslPath,
                                  const std::string& vertSpvPath,
                                  const std::string& fragSpvPath) {
    std::string output;

    std::string vertCmd = "naga --input-kind wgsl --keep-coordinate-space --shader-stage vert --entry-point vs_main "
                        + shell_quote(wgslPath) + " " + shell_quote(vertSpvPath) + " 2>&1";
    std::cout << "[fiction] Compiling WGSL particle vertex shader: " << wgslPath << std::endl;
    if (!run_command_capture(vertCmd, output)) {
        std::cerr << "[fiction] ERROR: WGSL vertex compilation failed for " << wgslPath << std::endl;
        if (!output.empty()) {
            std::cerr << "[fiction] Compiler output:\n" << output << std::endl;
        }
        return false;
    }
    if (!output.empty()) {
        std::cout << "[fiction] Compiler output:\n" << output << std::endl;
    }

    output.clear();
    std::string fragCmd = "naga --input-kind wgsl --keep-coordinate-space --shader-stage frag --entry-point fs_main "
                        + shell_quote(wgslPath) + " " + shell_quote(fragSpvPath) + " 2>&1";
    std::cout << "[fiction] Compiling WGSL particle fragment shader: " << wgslPath << std::endl;
    if (!run_command_capture(fragCmd, output)) {
        std::cerr << "[fiction] ERROR: WGSL fragment compilation failed for " << wgslPath << std::endl;
        if (!output.empty()) {
            std::cerr << "[fiction] Compiler output:\n" << output << std::endl;
        }
        return false;
    }
    if (!output.empty()) {
        std::cout << "[fiction] Compiler output:\n" << output << std::endl;
    }

    std::cout << "[fiction] Successfully compiled particle WGSL to SPIR-V" << std::endl;
    return true;
}

// Register a shader for hot reload watching.
// Call after the pipeline + layout are created.
inline void watch_shader(const std::string& name, bool alphaBlend,
                          VkPipeline* pipeline, VkPipelineLayout* pipelineLayout,
                          const char* vertEntry, const char* fragEntry) {
    auto* hr = get_shader_hot_reload();
    if (!hr) return;

    ShaderWatch w;
    w.name = name;
    w.alphaBlend = alphaBlend;
    w.vertEntry = vertEntry;
    w.fragEntry = fragEntry;
    w.pipeline = pipeline;
    w.pipelineLayout = pipelineLayout;

    const std::string wgslPath = hr->shaderDir + "/" + name + ".wgsl";
    w.wgslModTime = get_file_mod_time_shader(wgslPath);
    w.usesWgsl = (w.wgslModTime != 0);
    if (w.usesWgsl) {
        w.wgslPath = wgslPath;
    } else {
        // Watch GLSL source files, not compiled .spv files.
        w.vertModTime = get_file_mod_time_shader(hr->shaderDir + "/" + name + ".vert");
        w.fragModTime = get_file_mod_time_shader(hr->shaderDir + "/" + name + ".frag");
    }

    hr->watches.push_back(w);
}

inline void init_shader_hot_reload(const std::string& shaderDir) {
    auto* hr = new ShaderHotReload();
    get_shader_hot_reload() = hr;
    hr->shaderDir = shaderDir;
    hr->lastPollTime = std::chrono::steady_clock::now();
    hr->initialized = true;
    std::cout << "[fiction] Shader hot reload initialized, watching " << shaderDir << std::endl;
}

// Call each frame to check for shader changes
inline void poll_shader_hot_reload() {
    auto* hr = get_shader_hot_reload();
    if (!hr || !hr->initialized) return;

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - hr->lastPollTime).count();
    if (elapsed < hr->pollInterval) return;
    hr->lastPollTime = now;

    auto device = fiction_engine::get_device();
    auto renderPass = fiction_engine::get_render_pass();
    bool anyChanged = false;

    for (auto& w : hr->watches) {
        std::string vertSpv = hr->shaderDir + "/" + w.name + ".vert.spv";
        std::string fragSpv = hr->shaderDir + "/" + w.name + ".frag.spv";
        bool changed = false;

        if (w.usesWgsl) {
            time_t newWgsl = get_file_mod_time_shader(w.wgslPath);
            changed = (newWgsl != 0 && newWgsl != w.wgslModTime);
            if (!changed) {
                continue;
            }
            std::cout << "[fiction] Hot reload detected: " << w.name << ".wgsl changed" << std::endl;
            if (!compile_wgsl_particle(w.wgslPath, vertSpv, fragSpv)) {
                continue;
            }

            if (!anyChanged) {
                vkDeviceWaitIdle(device);
                anyChanged = true;
            }

            std::cout << "[fiction] Hot reload: reloading " << w.name << " pipeline..." << std::endl;
            if (hot_reload_pipeline(device, hr->shaderDir, w.name,
                                    *w.pipelineLayout, renderPass, w.alphaBlend,
                                    w.vertEntry, w.fragEntry, *w.pipeline)) {
                std::cout << "[fiction] Hot reload: " << w.name << " pipeline updated successfully"
                          << std::endl;
                w.wgslModTime = newWgsl;
            } else {
                std::cerr << "[fiction] Hot reload ERROR: " << w.name
                          << " pipeline recreation failed!" << std::endl;
            }
            continue;
        }

        const std::string vertSrc = hr->shaderDir + "/" + w.name + ".vert";
        const std::string fragSrc = hr->shaderDir + "/" + w.name + ".frag";
        time_t newVertSrc = get_file_mod_time_shader(vertSrc);
        time_t newFragSrc = get_file_mod_time_shader(fragSrc);

        bool vertChanged = (newVertSrc != w.vertModTime && newVertSrc != 0);
        bool fragChanged = (newFragSrc != w.fragModTime && newFragSrc != 0);
        changed = vertChanged || fragChanged;
        if (!changed) {
            continue;
        }

        std::cout << "[fiction] Hot reload detected: " << w.name << " source changed ("
                  << (vertChanged ? "vert " : "")
                  << (fragChanged ? "frag" : "")
                  << ")" << std::endl;

        if (vertChanged && !compile_shader_source(vertSrc, vertSpv, "vert")) {
            continue;
        }
        if (fragChanged && !compile_shader_source(fragSrc, fragSpv, "frag")) {
            continue;
        }

        if (!anyChanged) {
            vkDeviceWaitIdle(device);
            anyChanged = true;
        }

        std::cout << "[fiction] Hot reload: reloading " << w.name << " pipeline..." << std::endl;
        if (hot_reload_pipeline(device, hr->shaderDir, w.name,
                                *w.pipelineLayout, renderPass, w.alphaBlend,
                                w.vertEntry, w.fragEntry, *w.pipeline)) {
            std::cout << "[fiction] Hot reload: " << w.name << " pipeline updated successfully"
                      << std::endl;
            w.vertModTime = newVertSrc;
            w.fragModTime = newFragSrc;
        } else {
            std::cerr << "[fiction] Hot reload ERROR: " << w.name
                      << " pipeline recreation failed!" << std::endl;
        }
    }
}

inline void cleanup_shader_hot_reload() {
    auto* hr = get_shader_hot_reload();
    if (!hr) return;
    delete hr;
    get_shader_hot_reload() = nullptr;
}

// =============================================================================
// Create graphics pipeline for text rendering
// =============================================================================

inline bool create_text_pipeline(TextRenderer* tr, const std::string& shaderDir) {
    // Create descriptor set layout (binding 0 = font atlas sampler)
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(tr->device, &layoutInfo, nullptr, &tr->descriptorSetLayout);
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(tr->device, &poolInfo, nullptr, &tr->descriptorPool);
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = tr->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &tr->descriptorSetLayout;
    vkAllocateDescriptorSets(tr->device, &allocInfo, &tr->descriptorSet);
    
    // Update descriptor set with font atlas
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = tr->font.sampler;
    imageInfo.imageView = tr->font.imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = tr->descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(tr->device, 1, &write, 0, nullptr);
    
    // Push constant range for screen dimensions
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 2;  // screenWidth, screenHeight
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &tr->descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(tr->device, &pipelineLayoutInfo, nullptr, &tr->pipelineLayout);
    
    // Use generic pipeline creator (alpha blend for text)
    tr->pipeline = create_pipeline(tr->device, shaderDir, "text",
                                    tr->pipelineLayout, tr->renderPass, true);
    if (tr->pipeline == VK_NULL_HANDLE) {
        return false;
    }
    
    return true;
}

// =============================================================================
// Upload font atlas data to GPU using stb_truetype
// =============================================================================

inline void upload_font_atlas(TextRenderer* tr) {
    // Create staging buffer
    uint32_t atlasSize = tr->font.atlasWidth * tr->font.atlasHeight;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = atlasSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(tr->device, &bufInfo, nullptr, &stagingBuffer);
    
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(tr->device, stagingBuffer, &memReq);
    
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_text_memory_type(tr, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(tr->device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(tr->device, stagingBuffer, stagingMemory, 0);
    
    // Fill atlas with stb_truetype rendered glyphs
    void* data;
    vkMapMemory(tr->device, stagingMemory, 0, atlasSize, 0, &data);
    uint8_t* pixels = (uint8_t*)data;
    
    // Clear to black
    memset(pixels, 0, atlasSize);
    
    // Put a solid white block at (0,0) for backgrounds (first 32x32 pixels)
    for (uint32_t y = 0; y < 32; y++) {
        for (uint32_t x = 0; x < 32; x++) {
            pixels[y * tr->font.atlasWidth + x] = 255;
        }
    }
    
    // Check if font was loaded
    if (tr->font.fontData.empty()) {
        std::cerr << "[fiction] No font data, using blank atlas" << std::endl;
        vkUnmapMemory(tr->device, stagingMemory);
        goto copy_to_gpu;
    }
    
    {
        // Get codepoints to render
        std::vector<int32_t> codepoints = get_required_codepoints();
        
        // Pack glyphs into atlas using simple row-based packing
        int cursorX = 32;  // Start after solid white block
        int cursorY = 0;
        int rowHeight = 0;
        int padding = 2;
        
        for (int32_t cp : codepoints) {
            // Skip space - we handle it specially
            if (cp == ' ') {
                GlyphInfo gi;
                gi.u0 = 0; gi.v0 = 0; gi.u1 = 0.01f; gi.v1 = 0.01f;
                gi.width = 0;
                gi.height = 0;
                gi.advance = tr->font.spaceWidth;
                gi.xoffset = 0;
                gi.yoffset = 0;
                tr->font.glyphs[cp] = gi;
                continue;
            }
            
            // Get glyph metrics
            int glyphIndex = stbtt_FindGlyphIndex(&tr->font.fontInfo, cp);
            if (glyphIndex == 0 && cp != 0) {
                // Glyph not found, skip
                continue;
            }
            
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&tr->font.fontInfo, cp, &advance, &lsb);
            
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&tr->font.fontInfo, cp, tr->font.scale, tr->font.scale,
                                        &x0, &y0, &x1, &y1);
            
            int glyphW = x1 - x0;
            int glyphH = y1 - y0;
            
            if (glyphW <= 0 || glyphH <= 0) {
                // Empty glyph
                GlyphInfo gi;
                gi.u0 = 0; gi.v0 = 0; gi.u1 = 0; gi.v1 = 0;
                gi.width = 0;
                gi.height = 0;
                gi.advance = advance * tr->font.scale;
                gi.xoffset = 0;
                gi.yoffset = 0;
                tr->font.glyphs[cp] = gi;
                continue;
            }
            
            // Check if we need to wrap to next row
            if (cursorX + glyphW + padding > (int)tr->font.atlasWidth) {
                cursorX = 0;
                cursorY += rowHeight + padding;
                rowHeight = 0;
            }
            
            // Check if we're out of vertical space
            if (cursorY + glyphH > (int)tr->font.atlasHeight) {
                std::cerr << "[fiction] Font atlas full at codepoint " << cp << std::endl;
                break;
            }
            
            // Render glyph to atlas
            stbtt_MakeCodepointBitmap(&tr->font.fontInfo,
                                      pixels + cursorY * tr->font.atlasWidth + cursorX,
                                      glyphW, glyphH,
                                      tr->font.atlasWidth,  // stride
                                      tr->font.scale, tr->font.scale,
                                      cp);
            
            // Store glyph info
            GlyphInfo gi;
            gi.u0 = (float)cursorX / tr->font.atlasWidth;
            gi.v0 = (float)cursorY / tr->font.atlasHeight;
            gi.u1 = (float)(cursorX + glyphW) / tr->font.atlasWidth;
            gi.v1 = (float)(cursorY + glyphH) / tr->font.atlasHeight;
            gi.width = (float)glyphW;
            gi.height = (float)glyphH;
            gi.advance = advance * tr->font.scale;
            gi.xoffset = (float)x0;
            gi.yoffset = (float)y0 + tr->font.ascent;  // Adjust for baseline
            tr->font.glyphs[cp] = gi;
            
            // Advance cursor
            cursorX += glyphW + padding;
            rowHeight = std::max(rowHeight, glyphH);
        }
        
        std::cout << "[fiction] Rendered " << tr->font.glyphs.size() << " glyphs to atlas" << std::endl;
    }
    
    vkUnmapMemory(tr->device, stagingMemory);
    
copy_to_gpu:
    // Copy to image
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = tr->commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(tr->device, &cmdInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Transition image to transfer dst
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tr->font.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tr->font.atlasWidth, tr->font.atlasHeight, 1};
    
    vkCmdCopyBufferToImage(cmd, stagingBuffer, tr->font.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    vkEndCommandBuffer(cmd);
    
    // Submit and wait
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(tr->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(tr->graphicsQueue);
    
    // Cleanup
    vkFreeCommandBuffers(tr->device, tr->commandPool, 1, &cmd);
    vkDestroyBuffer(tr->device, stagingBuffer, nullptr);
    vkFreeMemory(tr->device, stagingMemory, nullptr);
    
    std::cout << "[fiction] Font atlas uploaded (" << tr->font.atlasWidth 
              << "x" << tr->font.atlasHeight << ")" << std::endl;
}

// =============================================================================
// Render particles to command buffer
// =============================================================================

inline void record_particle_commands(VkCommandBuffer cmd) {
    auto* pr = get_particle_renderer();
    auto* tr = get_text_renderer();
    if (!pr || !pr->initialized || !tr || pr->vertexCount == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pr->pipeline);

    // Uniforms: {screenWidth, screenHeight, elapsedTime, yFlip}
    float elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - pr->startTimePoint).count();
    float uniformData[4] = {tr->screenWidth, tr->screenHeight, elapsed, 1.0f};
    std::memcpy(pr->uniformMapped, uniformData, sizeof(uniformData));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pr->pipelineLayout,
                            0, 1, &pr->descriptorSet, 0, nullptr);

    VkViewport viewport{};
    viewport.width = tr->screenWidth;
    viewport.height = tr->screenHeight;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {(uint32_t)tr->screenWidth, (uint32_t)tr->screenHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &pr->vertexBuffer, &offset);
    vkCmdDraw(cmd, pr->vertexCount, 1, 0, 0);

    // Clear speaker quads for next frame
    pr->speakerQuads.clear();
}

// =============================================================================
// Render text to command buffer
// =============================================================================

inline void record_text_commands(VkCommandBuffer cmd) {
    // Poll for shader hot reload changes
    poll_shader_hot_reload();
    
    // Draw background first (behind everything)
    record_bg_commands(cmd);
    
    // Draw particles (between bg and text)
    record_particle_commands(cmd);
    
    TextRenderer* tr = get_text_renderer();
    if (!tr || !tr->pipeline || tr->vertexCount == 0) return;
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tr->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tr->pipelineLayout,
                            0, 1, &tr->descriptorSet, 0, nullptr);
    
    // Push screen dimensions
    float screenSize[2] = {tr->screenWidth, tr->screenHeight};
    vkCmdPushConstants(cmd, tr->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(screenSize), screenSize);
    
    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = tr->screenWidth;
    viewport.height = tr->screenHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)tr->screenWidth, (uint32_t)tr->screenHeight};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Bind vertex buffer
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &tr->vertexBuffer, &offset);
    
    // Draw
    vkCmdDraw(cmd, tr->vertexCount, 1, 0, 0);
}

// =============================================================================
// Initialization
// =============================================================================

// Forward declaration for simple init
inline bool init_text_renderer(VkDevice device, 
                               VkPhysicalDevice physicalDevice,
                               VkQueue graphicsQueue,
                               VkCommandPool commandPool,
                               VkRenderPass renderPass,
                               float screenWidth,
                               float screenHeight,
                               const std::string& shaderDir);

// Simple init that gets Vulkan handles from fiction_engine
inline bool init_text_renderer_simple(float screenWidth,
                                      float screenHeight,
                                      const std::string& shaderDir = "vulkan_fiction") {
    return init_text_renderer(
        fiction_engine::get_device(),
        fiction_engine::get_physical_device(),
        fiction_engine::get_graphics_queue(),
        fiction_engine::get_command_pool(),
        fiction_engine::get_render_pass(),
        screenWidth,
        screenHeight,
        shaderDir
    );
}

inline bool init_text_renderer(VkDevice device, 
                               VkPhysicalDevice physicalDevice,
                               VkQueue graphicsQueue,
                               VkCommandPool commandPool,
                               VkRenderPass renderPass,
                               float screenWidth,
                               float screenHeight,
                               const std::string& shaderDir = "vulkan_kim") {
    TextRenderer* tr = new TextRenderer();
    get_text_renderer() = tr;
    
    tr->device = device;
    tr->physicalDevice = physicalDevice;
    tr->graphicsQueue = graphicsQueue;
    tr->commandPool = commandPool;
    tr->renderPass = renderPass;
    tr->screenWidth = screenWidth;
    tr->screenHeight = screenHeight;
    
    // Initialize shader hot reload BEFORE creating any pipelines
    init_shader_hot_reload(shaderDir);
    
    // Create font atlas (Vulkan resources)
    create_simple_font_atlas(tr);
    
    // Upload font data to GPU
    upload_font_atlas(tr);
    
    // Create vertex buffer
    create_text_vertex_buffer(tr);
    
    // Create text rendering pipeline
    if (!create_text_pipeline(tr, shaderDir)) {
        cleanup_text_renderer();
        return false;
    }
    
    // Register text pipeline for hot reload
    watch_shader("text", true, &tr->pipeline, &tr->pipelineLayout);
    
    // Initialize particle renderer (optional — continues if shaders missing)
    init_particle_renderer(device, physicalDevice, renderPass, shaderDir);
    auto* pr = get_particle_renderer();
    if (pr && pr->initialized) {
        watch_shader("particle", true, &pr->pipeline, &pr->pipelineLayout, "vs_main", "fs_main");
    }
    
    tr->initialized = true;
    
    // Register render callback with the engine
    fiction_engine::set_render_callback(record_text_commands);
    
    std::cout << "[fiction] Text renderer initialized" << std::endl;
    return true;
}

inline void cleanup_text_renderer() {
    // Clean up sub-renderers and hot reload first
    cleanup_particle_renderer();
    cleanup_shader_hot_reload();
    cleanup_bg_renderer();
    
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    
    if (tr->vertexBuffer) {
        vkUnmapMemory(tr->device, tr->vertexMemory);
        vkDestroyBuffer(tr->device, tr->vertexBuffer, nullptr);
        vkFreeMemory(tr->device, tr->vertexMemory, nullptr);
    }
    
    if (tr->font.sampler) vkDestroySampler(tr->device, tr->font.sampler, nullptr);
    if (tr->font.imageView) vkDestroyImageView(tr->device, tr->font.imageView, nullptr);
    if (tr->font.image) vkDestroyImage(tr->device, tr->font.image, nullptr);
    if (tr->font.memory) vkFreeMemory(tr->device, tr->font.memory, nullptr);
    
    if (tr->pipeline) vkDestroyPipeline(tr->device, tr->pipeline, nullptr);
    if (tr->pipelineLayout) vkDestroyPipelineLayout(tr->device, tr->pipelineLayout, nullptr);
    if (tr->descriptorPool) vkDestroyDescriptorPool(tr->device, tr->descriptorPool, nullptr);
    if (tr->descriptorSetLayout) vkDestroyDescriptorSetLayout(tr->device, tr->descriptorSetLayout, nullptr);
    
    delete tr;
    get_text_renderer() = nullptr;
}

// =============================================================================
// Scroll Control
// =============================================================================

inline void scroll_dialogue(float delta) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    
    // User manually scrolling - cancel auto-scroll and let them control
    tr->isAutoScrolling = false;
    tr->scrollOffset = std::max(0.0f, std::min(tr->maxScroll, tr->scrollOffset + delta));
}

inline void scroll_to_bottom(bool animated = true) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    
    if (animated) {
        tr->autoScrollEnabled = true;
        tr->targetScrollOffset = tr->maxScroll;
    } else {
        tr->scrollOffset = tr->maxScroll;
        tr->targetScrollOffset = tr->maxScroll;
    }
}

// =============================================================================
// Jank-callable API
// =============================================================================

inline bool text_renderer_initialized() {
    TextRenderer* tr = get_text_renderer();
    return tr && tr->initialized;
}

inline void set_panel_colors(float r, float g, float b, float a) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.bgR = r; tr->style.bgG = g; tr->style.bgB = b; tr->style.bgA = a;
}

inline void set_panel_position(float x, float width) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.panelX = x;
    tr->style.panelWidth = width;
}

// Set text and speaker scale
inline void set_text_scale(float scale) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.textScale = scale;
}

inline void set_speaker_scale(float scale) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.speakerScale = scale;
}

// Set spacing
inline void set_line_spacing(float spacing) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.lineSpacing = spacing;
}

inline void set_entry_spacing(float spacing) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.entrySpacing = spacing;
}

inline void set_panel_padding(float padding) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.panelPadding = padding;
}

inline void set_choice_indent(float indent) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.choiceIndent = indent;
}

// Set text colors
inline void set_text_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.textR = r; tr->style.textG = g; tr->style.textB = b;
}

inline void set_narration_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.narrationR = r; tr->style.narrationG = g; tr->style.narrationB = b;
}

inline void set_choice_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.choiceR = r; tr->style.choiceG = g; tr->style.choiceB = b;
}

inline void set_choice_hover_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.choiceHoverR = r; tr->style.choiceHoverG = g; tr->style.choiceHoverB = b;
}

inline void set_choice_selected_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.choiceSelectedR = r; tr->style.choiceSelectedG = g; tr->style.choiceSelectedB = b;
}

inline void set_choice_selected_hover_color(float r, float g, float b) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->style.choiceSelectedHoverR = r; tr->style.choiceSelectedHoverG = g; tr->style.choiceSelectedHoverB = b;
}

inline uint32_t get_text_vertex_count() {
    TextRenderer* tr = get_text_renderer();
    return tr ? tr->vertexCount : 0;
}

inline VkBuffer get_text_vertex_buffer() {
    TextRenderer* tr = get_text_renderer();
    return tr ? tr->vertexBuffer : VK_NULL_HANDLE;
}

// =============================================================================
// Jank-friendly Dialogue Building API
// =============================================================================
// These functions allow building dialogue entries one at a time from jank,
// avoiding the need to pass C++ vectors across the FFI boundary.

// Stored entries for current frame
inline std::vector<DialogueEntry>& get_pending_history() {
    static std::vector<DialogueEntry> history;
    return history;
}

inline std::vector<DialogueEntry>& get_pending_choices() {
    static std::vector<DialogueEntry> choices;
    return choices;
}

// Clear pending entries (call at start of frame)
inline void clear_pending_entries() {
    get_pending_history().clear();
    get_pending_choices().clear();
}

// Add a dialogue entry to history
// type: 0=Dialogue, 1=Narration, 2=Choice, 3=ChoiceSelected
inline void add_history_entry(int type, const char* speaker, const char* text,
                               float speakerR, float speakerG, float speakerB,
                               bool selected) {
    DialogueEntry entry;
    entry.type = static_cast<EntryType>(type);
    entry.speaker = speaker ? speaker : "";
    entry.text = text ? text : "";
    entry.speakerR = speakerR;
    entry.speakerG = speakerG;
    entry.speakerB = speakerB;
    entry.selected = selected;
    get_pending_history().push_back(entry);
}

// Add a choice entry with selected status
inline void add_choice_entry_with_selected(const char* text, bool selected) {
    DialogueEntry entry;
    entry.type = selected ? EntryType::ChoiceSelected : EntryType::Choice;
    entry.speaker = "";
    entry.text = text ? text : "";
    entry.speakerR = 0.85f;
    entry.speakerG = 0.55f;
    entry.speakerB = 0.25f;
    entry.selected = selected;
    get_pending_choices().push_back(entry);
}

// Add a choice entry (convenience wrapper)
inline void add_choice_entry(const char* text) {
    add_choice_entry_with_selected(text, false);
}

// Build vertices from pending entries
inline void build_dialogue_from_pending() {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    
    render_dialogue_panel(tr, get_pending_history(), get_pending_choices());
}

// Convenience: Get pending entry counts
inline int get_pending_history_count() {
    return static_cast<int>(get_pending_history().size());
}

inline int get_pending_choices_count() {
    return static_cast<int>(get_pending_choices().size());
}

// =============================================================================
// Mouse Interaction API
// =============================================================================

// Update mouse position (call each frame with current mouse coords)
inline void update_mouse_position(float x, float y) {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    tr->mouseX = x;
    tr->mouseY = y;
}

// Get the currently hovered choice index (-1 if none)
inline int get_hovered_choice() {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return -1;
    return tr->hoveredChoice;
}

// Get clicked choice based on current mouse position (-1 if not on a choice)
// Call this when mouse button is pressed
inline int get_clicked_choice() {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return -1;
    return tr->hoveredChoice;  // If hovered, that's what we're clicking
}

} // namespace fiction
