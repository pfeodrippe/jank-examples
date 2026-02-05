// Fiction Text Renderer - Vulkan-based text rendering for narrative games
// Uses SDF (Signed Distance Field) fonts for crisp text at any resolution
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

namespace fiction {

// =============================================================================
// Font Glyph Data (using embedded bitmap font for simplicity)
// =============================================================================

// Simple 8x16 bitmap font - ASCII 32-126
// Each character is 8 pixels wide, 16 pixels tall
// This is a placeholder - in production use stb_truetype or FreeType

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
    
    uint32_t atlasWidth = 256;
    uint32_t atlasHeight = 256;
    
    std::unordered_map<char, GlyphInfo> glyphs;
    float lineHeight = 20.0f;
    float spaceWidth = 8.0f;
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
// Text Renderer State
// =============================================================================

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
    
    // Panel layout
    float panelX = 0.70f;      // Start at 70% of screen width
    float panelWidth = 0.30f;  // 30% of screen
    float panelPadding = 20.0f;
    float lineSpacing = 4.0f;
    
    // Scroll state
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    
    // Colors
    float bgR = 0.08f, bgG = 0.08f, bgB = 0.08f, bgA = 0.95f;
    
    bool initialized = false;
};

// =============================================================================
// ODR-safe global accessor
// =============================================================================

inline TextRenderer*& get_text_renderer() {
    static TextRenderer* ptr = nullptr;
    return ptr;
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
// Font Atlas Creation (Simple Embedded Font)
// =============================================================================

// Embedded 8x8 font data for ASCII 32-126 (simplified)
// In production, use stb_truetype to load actual fonts

inline void create_simple_font_atlas(TextRenderer* tr) {
    // Create a simple 256x256 atlas with basic ASCII characters
    // For now, just create a white texture - actual glyph rendering would use stb_truetype
    
    tr->font.atlasWidth = 256;
    tr->font.atlasHeight = 256;
    
    // Create image
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
    
    // Setup glyph info for ASCII 32-126
    // Simple 16x16 grid layout: 16 chars per row, 6 rows
    float charWidth = 1.0f / 16.0f;
    float charHeight = 1.0f / 16.0f;
    
    for (int c = 32; c < 127; c++) {
        int idx = c - 32;
        int col = idx % 16;
        int row = idx / 16;
        
        GlyphInfo gi;
        gi.u0 = col * charWidth;
        gi.v0 = row * charHeight;
        gi.u1 = gi.u0 + charWidth;
        gi.v1 = gi.v0 + charHeight;
        gi.width = 8.0f;
        gi.height = 16.0f;  // Default height
        gi.advance = 9.0f;
        gi.xoffset = 0.0f;
        gi.yoffset = 0.0f;
        
        tr->font.glyphs[(char)c] = gi;
    }
    
    tr->font.lineHeight = 18.0f;
    tr->font.spaceWidth = 6.0f;
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

// Render a string at position, returns ending X position
inline float render_text_string(TextRenderer* tr,
                                const std::string& text,
                                float x, float y,
                                float scale,
                                float r, float g, float b, float a) {
    float cursorX = x;
    
    for (char c : text) {
        if (c == ' ') {
            cursorX += tr->font.spaceWidth * scale;
            continue;
        }
        
        auto it = tr->font.glyphs.find(c);
        if (it == tr->font.glyphs.end()) {
            cursorX += tr->font.spaceWidth * scale;
            continue;
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

// Word-wrap text and return lines
inline std::vector<std::string> wrap_text(TextRenderer* tr, 
                                          const std::string& text,
                                          float maxWidth,
                                          float scale) {
    std::vector<std::string> lines;
    std::string currentLine;
    float currentWidth = 0.0f;
    
    size_t i = 0;
    while (i < text.size()) {
        // Find next word
        size_t wordStart = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
            i++;
        }
        std::string word = text.substr(wordStart, i - wordStart);
        
        // Measure word width
        float wordWidth = 0.0f;
        for (char c : word) {
            auto it = tr->font.glyphs.find(c);
            if (it != tr->font.glyphs.end()) {
                wordWidth += it->second.advance * scale;
            } else {
                wordWidth += tr->font.spaceWidth * scale;
            }
        }
        
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
        
        // Handle newlines
        if (i < text.size() && text[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
            currentWidth = 0.0f;
        }
        
        // Skip spaces
        while (i < text.size() && text[i] == ' ') {
            i++;
        }
        if (i < text.size() && text[i] == '\n') {
            i++;
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
                                   float scale) {
    float panelStartX = tr->screenWidth * tr->panelX + tr->panelPadding;
    float textWidth = tr->screenWidth * tr->panelWidth - tr->panelPadding * 2;
    float lineH = tr->font.lineHeight * scale + tr->lineSpacing;
    
    float currentY = y;
    
    // Colors based on entry type
    float textR = 0.8f, textG = 0.8f, textB = 0.8f;
    
    switch (entry.type) {
        case EntryType::Choice:
            textR = 0.85f; textG = 0.55f; textB = 0.25f;  // Orange
            break;
        case EntryType::ChoiceSelected:
            textR = 0.5f; textG = 0.5f; textB = 0.5f;     // Muted grey
            break;
        case EntryType::Narration:
            textR = 0.75f; textG = 0.75f; textB = 0.78f;  // Slightly blue-grey
            break;
        default:
            break;
    }
    
    // Render speaker name if present
    if (!entry.speaker.empty()) {
        std::string speakerLabel = entry.speaker + " ---";
        render_text_string(tr, speakerLabel, panelStartX, currentY, scale,
                          entry.speakerR, entry.speakerG, entry.speakerB, 1.0f);
        currentY += lineH;
    }
    
    // Word-wrap and render the text
    auto lines = wrap_text(tr, entry.text, textWidth, scale);
    for (const auto& line : lines) {
        // Add indent for choices
        float indent = (entry.type == EntryType::Choice || 
                        entry.type == EntryType::ChoiceSelected) ? 20.0f : 0.0f;
        
        render_text_string(tr, line, panelStartX + indent, currentY, scale,
                          textR, textG, textB, 1.0f);
        currentY += lineH;
    }
    
    // Add spacing after entry
    currentY += tr->lineSpacing * 2;
    
    return currentY;
}

inline void render_dialogue_panel(TextRenderer* tr,
                                  const std::vector<DialogueEntry>& history,
                                  const std::vector<DialogueEntry>& choices) {
    tr->vertexCount = 0;  // Reset vertices
    
    float panelX = tr->screenWidth * tr->panelX;
    float panelW = tr->screenWidth * tr->panelWidth;
    float panelH = tr->screenHeight;
    
    // Draw panel background
    add_rect(tr, panelX, 0, panelW, panelH,
             tr->bgR, tr->bgG, tr->bgB, tr->bgA);
    
    // Film strip edge decoration (right side)
    float stripWidth = 15.0f;
    add_rect(tr, tr->screenWidth - stripWidth, 0, stripWidth, panelH,
             0.05f, 0.05f, 0.05f, 1.0f);
    
    // Render dialogue history
    float scale = 1.0f;
    float y = tr->panelPadding - tr->scrollOffset;
    
    for (const auto& entry : history) {
        if (y > tr->screenHeight) break;  // Off-screen below
        
        float entryHeight = 0.0f;
        // Only render if visible
        if (y + 200 > 0) {  // Rough visibility check
            y = render_dialogue_entry(tr, entry, y, scale);
        } else {
            // Skip but account for height
            auto lines = wrap_text(tr, entry.text, 
                                   tr->screenWidth * tr->panelWidth - tr->panelPadding * 2,
                                   scale);
            float lineH = tr->font.lineHeight * scale + tr->lineSpacing;
            y += (!entry.speaker.empty() ? lineH : 0);
            y += lines.size() * lineH;
            y += tr->lineSpacing * 2;
        }
    }
    
    // Separator before choices
    if (!choices.empty()) {
        float sepY = y + 10;
        add_rect(tr, panelX + tr->panelPadding, sepY, 
                 panelW - tr->panelPadding * 2, 1.0f,
                 0.4f, 0.4f, 0.4f, 0.5f);
        y = sepY + 20;
    }
    
    // Render choices
    int choiceNum = 1;
    for (const auto& choice : choices) {
        // Add number prefix
        DialogueEntry numberedChoice = choice;
        numberedChoice.text = std::to_string(choiceNum) + ". " + choice.text;
        y = render_dialogue_entry(tr, numberedChoice, y, scale);
        choiceNum++;
    }
    
    // Update max scroll
    tr->maxScroll = std::max(0.0f, y + tr->scrollOffset - tr->screenHeight + 100);
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
// Create graphics pipeline for text rendering
// =============================================================================

inline bool create_text_pipeline(TextRenderer* tr, const std::string& shaderDir) {
    // Load pre-compiled SPIR-V shaders
    auto vertSpirv = load_text_spirv(shaderDir + "/text.vert.spv");
    auto fragSpirv = load_text_spirv(shaderDir + "/text.frag.spv");
    
    if (vertSpirv.empty() || fragSpirv.empty()) {
        std::cerr << "[fiction] Failed to load text shaders from " << shaderDir << std::endl;
        return false;
    }
    
    // Create shader modules
    VkShaderModule vertModule, fragModule;
    
    VkShaderModuleCreateInfo vertInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vertInfo.codeSize = vertSpirv.size() * sizeof(uint32_t);
    vertInfo.pCode = vertSpirv.data();
    vkCreateShaderModule(tr->device, &vertInfo, nullptr, &vertModule);
    
    VkShaderModuleCreateInfo fragInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fragInfo.codeSize = fragSpirv.size() * sizeof(uint32_t);
    fragInfo.pCode = fragSpirv.data();
    vkCreateShaderModule(tr->device, &fragInfo, nullptr, &fragModule);
    
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
    
    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";
    
    // Vertex input: position (vec2), texcoord (vec2), color (vec4)
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(TextVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    VkVertexInputAttributeDescription attrDescs[3] = {};
    // Position
    attrDescs[0].location = 0;
    attrDescs[0].binding = 0;
    attrDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[0].offset = offsetof(TextVertex, x);
    // TexCoord
    attrDescs[1].location = 1;
    attrDescs[1].binding = 0;
    attrDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDescs[1].offset = offsetof(TextVertex, u);
    // Color
    attrDescs[2].location = 2;
    attrDescs[2].binding = 0;
    attrDescs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDescs[2].offset = offsetof(TextVertex, r);
    
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrDescs;
    
    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    // Viewport/scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    
    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Depth stencil (disabled for 2D text overlay)
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    
    // Blending (alpha blending for text)
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;
    
    // Dynamic state
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = tr->pipelineLayout;
    pipelineInfo.renderPass = tr->renderPass;
    pipelineInfo.subpass = 0;
    
    VkResult result = vkCreateGraphicsPipelines(tr->device, VK_NULL_HANDLE, 1, 
                                                 &pipelineInfo, nullptr, &tr->pipeline);
    
    // Cleanup shader modules
    vkDestroyShaderModule(tr->device, vertModule, nullptr);
    vkDestroyShaderModule(tr->device, fragModule, nullptr);
    
    if (result != VK_SUCCESS) {
        std::cerr << "[fiction] Failed to create text pipeline" << std::endl;
        return false;
    }
    
    std::cout << "[fiction] Text pipeline created successfully" << std::endl;
    return true;
}

// =============================================================================
// Upload font atlas data to GPU
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
    
    // Fill with embedded 8x8 bitmap font data
    void* data;
    vkMapMemory(tr->device, stagingMemory, 0, atlasSize, 0, &data);
    uint8_t* pixels = (uint8_t*)data;
    
    // Clear to black
    memset(pixels, 0, atlasSize);
    
    // Put a solid white block at UV (0,0) for backgrounds (first 16x16 pixels)
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            pixels[y * tr->font.atlasWidth + x] = 255;
        }
    }
    
    // 8x8 bitmap font data for ASCII 32-126
    // Each character is 8 bytes, one byte per row (8 bits = 8 pixels)
    // This is a classic CP437-style bitmap font
    static const uint8_t font8x8[95][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 (space)
        {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 !
        {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // 34 "
        {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // 35 #
        {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // 36 $
        {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // 37 %
        {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // 38 &
        {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // 39 '
        {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // 40 (
        {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // 41 )
        {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 *
        {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // 43 +
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // 44 ,
        {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // 45 -
        {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // 46 .
        {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // 47 /
        {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 48 0
        {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 49 1
        {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 50 2
        {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 51 3
        {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 52 4
        {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 53 5
        {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 54 6
        {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 55 7
        {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 56 8
        {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 57 9
        {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // 58 :
        {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // 59 ;
        {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // 60 <
        {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // 61 =
        {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 62 >
        {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // 63 ?
        {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // 64 @
        {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 65 A
        {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 66 B
        {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 67 C
        {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 68 D
        {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 69 E
        {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 70 F
        {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 71 G
        {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 72 H
        {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 73 I
        {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 74 J
        {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 75 K
        {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 76 L
        {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 77 M
        {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 78 N
        {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 79 O
        {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 80 P
        {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 81 Q
        {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 82 R
        {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 83 S
        {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 84 T
        {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 85 U
        {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 86 V
        {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 W
        {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 88 X
        {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 89 Y
        {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 90 Z
        {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // 91 [
        {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // 92 backslash
        {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // 93 ]
        {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // 94 ^
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 _
        {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // 96 `
        {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 97 a
        {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 98 b
        {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 99 c
        {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // 100 d
        {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // 101 e
        {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // 102 f
        {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 103 g
        {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 104 h
        {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 105 i
        {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 106 j
        {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 107 k
        {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 108 l
        {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 109 m
        {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 110 n
        {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 111 o
        {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 112 p
        {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 113 q
        {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 114 r
        {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 115 s
        {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 116 t
        {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 117 u
        {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 118 v
        {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 119 w
        {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 120 x
        {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 121 y
        {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 122 z
        {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // 123 {
        {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 |
        {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // 125 }
        {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // 126 ~
    };
    
    const int charW = 8, charH = 8;
    const int charsPerRow = 16;
    
    // Render each character to the atlas
    for (int c = 32; c < 127; c++) {
        int idx = c - 32;
        int col = idx % charsPerRow;
        int row = idx / charsPerRow + 1;  // +1 to skip solid white row
        
        const uint8_t* charData = font8x8[idx];
        
        for (int py = 0; py < 8; py++) {
            uint8_t rowBits = charData[py];
            for (int px = 0; px < 8; px++) {
                // Bit 0 is leftmost pixel in this font data
                bool on = (rowBits >> px) & 1;
                int x = col * charW + px;
                int y = row * charH + py;
                if (x < (int)tr->font.atlasWidth && y < (int)tr->font.atlasHeight) {
                    pixels[y * tr->font.atlasWidth + x] = on ? 255 : 0;
                }
            }
        }
    }
    
    // Update glyph UV coordinates for the new layout
    float charWidthUV = (float)charW / tr->font.atlasWidth;
    float charHeightUV = (float)charH / tr->font.atlasHeight;
    
    for (int c = 32; c < 127; c++) {
        int idx = c - 32;
        int col = idx % charsPerRow;
        int row = idx / charsPerRow + 1;
        
        GlyphInfo& gi = tr->font.glyphs[(char)c];
        gi.u0 = col * charWidthUV;
        gi.v0 = row * charHeightUV;
        gi.u1 = gi.u0 + charWidthUV;
        gi.v1 = gi.v0 + charHeightUV;
        gi.width = 8.0f;
        gi.height = 8.0f;
        gi.advance = 9.0f;
        gi.xoffset = 0.0f;
        gi.yoffset = 0.0f;
    }
    
    vkUnmapMemory(tr->device, stagingMemory);
    
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
// Render text to command buffer
// =============================================================================

inline void record_text_commands(VkCommandBuffer cmd) {
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
    
    tr->initialized = true;
    
    // Register text render callback with the engine
    fiction_engine::set_render_callback(record_text_commands);
    
    std::cout << "[fiction] Text renderer initialized" << std::endl;
    return true;
}

inline void cleanup_text_renderer() {
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
    
    tr->scrollOffset = std::max(0.0f, std::min(tr->maxScroll, tr->scrollOffset + delta));
}

inline void scroll_to_bottom() {
    TextRenderer* tr = get_text_renderer();
    if (!tr) return;
    
    tr->scrollOffset = tr->maxScroll;
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
    tr->bgR = r; tr->bgG = g; tr->bgB = b; tr->bgA = a;
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

// Add a choice entry
inline void add_choice_entry(const char* text) {
    DialogueEntry entry;
    entry.type = EntryType::Choice;
    entry.speaker = "";
    entry.text = text ? text : "";
    entry.speakerR = 0.85f;
    entry.speakerG = 0.55f;
    entry.speakerB = 0.25f;
    entry.selected = false;
    get_pending_choices().push_back(entry);
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

} // namespace fiction
