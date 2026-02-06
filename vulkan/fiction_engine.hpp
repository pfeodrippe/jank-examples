// Fiction Engine - Standalone Vulkan engine for narrative games
// Disco Elysium-style dialogue system with SDL3 + Vulkan
//
// This is a minimal Vulkan setup specifically for 2D text rendering,
// NOT dependent on the SDF engine.

#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>
#include <set>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace fiction_engine {

const uint32_t WINDOW_WIDTH = 1280;
const uint32_t WINDOW_HEIGHT = 720;

struct Engine {
    SDL_Window* window = nullptr;
    bool running = true;
    
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    
    uint32_t framebufferWidth = WINDOW_WIDTH;
    uint32_t framebufferHeight = WINDOW_HEIGHT;
    
    bool initialized = false;
};

// ODR-safe global accessor
inline Engine*& get_engine() {
    static Engine* ptr = nullptr;
    return ptr;
}

// Find suitable memory type
inline uint32_t find_memory_type(Engine* e, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(e->physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

// =============================================================================
// Initialization
// =============================================================================

inline bool init(const char* title) {
    if (get_engine() && get_engine()->initialized) {
        return true;
    }
    
    get_engine() = new Engine();
    auto* e = get_engine();
    
    // Init SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    e->window = SDL_CreateWindow(
        title,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!e->window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Bring window to front and focus it
    SDL_RaiseWindow(e->window);
    SDL_SetWindowFocusable(e->window, true);
    
    // Get actual framebuffer size
    int fbWidth, fbHeight;
    SDL_GetWindowSizeInPixels(e->window, &fbWidth, &fbHeight);
    e->framebufferWidth = (uint32_t)fbWidth;
    e->framebufferHeight = (uint32_t)fbHeight;
    
    // Create Vulkan instance
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = title;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Fiction";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
    
    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < sdlExtCount; i++) {
        extensions.push_back(sdlExts[i]);
    }
    
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.flags = 0;
    
#if defined(__APPLE__) && !TARGET_OS_IPHONE
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    
    if (vkCreateInstance(&instanceInfo, nullptr, &e->instance) != VK_SUCCESS) {
        std::cerr << "Vulkan instance creation failed" << std::endl;
        return false;
    }
    
    if (!SDL_Vulkan_CreateSurface(e->window, e->instance, nullptr, &e->surface)) {
        std::cerr << "Surface creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Pick physical device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(e->instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(e->instance, &deviceCount, devices.data());
    e->physicalDevice = devices[0];
    
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(e->physicalDevice, &props);
    std::cout << "[fiction] GPU: " << props.deviceName << std::endl;
    
    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(e->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(e->physicalDevice, &queueFamilyCount, queueFamilies.data());
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) e->graphicsFamily = i;
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(e->physicalDevice, i, e->surface, &presentSupport);
        if (presentSupport) e->presentFamily = i;
    }
    
    // Create logical device
    std::set<uint32_t> uniqueFamilies = {e->graphicsFamily, e->presentFamily};
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(qci);
    }
    
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(__APPLE__)
        "VK_KHR_portability_subset"
#endif
    };
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    
    if (vkCreateDevice(e->physicalDevice, &deviceInfo, nullptr, &e->device) != VK_SUCCESS) {
        std::cerr << "Device creation failed" << std::endl;
        return false;
    }
    
    vkGetDeviceQueue(e->device, e->graphicsFamily, 0, &e->graphicsQueue);
    vkGetDeviceQueue(e->device, e->presentFamily, 0, &e->presentQueue);
    
    // Create swapchain
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e->physicalDevice, e->surface, &capabilities);
    
    e->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    e->swapchainExtent = {e->framebufferWidth, e->framebufferHeight};
    
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainInfo.surface = e->surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = e->swapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = e->swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    uint32_t queueFamilyIndices[] = {e->graphicsFamily, e->presentFamily};
    if (e->graphicsFamily != e->presentFamily) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    
    if (vkCreateSwapchainKHR(e->device, &swapchainInfo, nullptr, &e->swapchain) != VK_SUCCESS) {
        std::cerr << "Swapchain creation failed" << std::endl;
        return false;
    }
    
    vkGetSwapchainImagesKHR(e->device, e->swapchain, &imageCount, nullptr);
    e->swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(e->device, e->swapchain, &imageCount, e->swapchainImages.data());
    
    // Create image views
    e->swapchainImageViews.resize(e->swapchainImages.size());
    for (size_t i = 0; i < e->swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = e->swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = e->swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(e->device, &viewInfo, nullptr, &e->swapchainImageViews[i]);
    }
    
    // Create render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = e->swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    
    if (vkCreateRenderPass(e->device, &renderPassInfo, nullptr, &e->renderPass) != VK_SUCCESS) {
        std::cerr << "Render pass creation failed" << std::endl;
        return false;
    }
    
    // Create framebuffers
    e->framebuffers.resize(e->swapchainImageViews.size());
    for (size_t i = 0; i < e->swapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbInfo.renderPass = e->renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &e->swapchainImageViews[i];
        fbInfo.width = e->swapchainExtent.width;
        fbInfo.height = e->swapchainExtent.height;
        fbInfo.layers = 1;
        vkCreateFramebuffer(e->device, &fbInfo, nullptr, &e->framebuffers[i]);
    }
    
    // Create command pool
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = e->graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(e->device, &poolInfo, nullptr, &e->commandPool);
    
    // Create command buffers
    e->commandBuffers.resize(e->swapchainImages.size());
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = e->commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = static_cast<uint32_t>(e->commandBuffers.size());
    vkAllocateCommandBuffers(e->device, &cmdInfo, e->commandBuffers.data());
    
    // Create sync objects
    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    vkCreateSemaphore(e->device, &semInfo, nullptr, &e->imageAvailableSemaphore);
    vkCreateSemaphore(e->device, &semInfo, nullptr, &e->renderFinishedSemaphore);
    vkCreateFence(e->device, &fenceInfo, nullptr, &e->inFlightFence);
    
    e->initialized = true;
    std::cout << "[fiction] Engine initialized (" << e->framebufferWidth 
              << "x" << e->framebufferHeight << ")" << std::endl;
    return true;
}

// =============================================================================
// Cleanup
// =============================================================================

inline void cleanup() {
    auto* e = get_engine();
    if (!e) return;
    
    vkDeviceWaitIdle(e->device);
    
    vkDestroySemaphore(e->device, e->imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(e->device, e->renderFinishedSemaphore, nullptr);
    vkDestroyFence(e->device, e->inFlightFence, nullptr);
    
    vkDestroyCommandPool(e->device, e->commandPool, nullptr);
    
    for (auto fb : e->framebuffers) {
        vkDestroyFramebuffer(e->device, fb, nullptr);
    }
    
    vkDestroyRenderPass(e->device, e->renderPass, nullptr);
    
    for (auto iv : e->swapchainImageViews) {
        vkDestroyImageView(e->device, iv, nullptr);
    }
    
    vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    vkDestroyDevice(e->device, nullptr);
    vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
    vkDestroyInstance(e->instance, nullptr);
    
    SDL_DestroyWindow(e->window);
    SDL_Quit();
    
    delete e;
    get_engine() = nullptr;
    
    std::cout << "[fiction] Engine cleaned up" << std::endl;
}

// =============================================================================
// Frame Rendering
// =============================================================================

// Callback type for custom rendering during the render pass
typedef void (*RenderCallback)(VkCommandBuffer cmd);

inline RenderCallback& get_render_callback() {
    static RenderCallback cb = nullptr;
    return cb;
}

inline void set_render_callback(RenderCallback cb) {
    get_render_callback() = cb;
}

inline void draw_frame() {
    auto* e = get_engine();
    if (!e) return;
    
    // Wait for previous frame
    vkWaitForFences(e->device, 1, &e->inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(e->device, 1, &e->inFlightFence);
    
    // Acquire image
    uint32_t imageIndex;
    vkAcquireNextImageKHR(e->device, e->swapchain, UINT64_MAX,
                          e->imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    
    // Record command buffer
    VkCommandBuffer cmd = e->commandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);
    
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // Begin render pass
    VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpInfo.renderPass = e->renderPass;
    rpInfo.framebuffer = e->framebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = e->swapchainExtent;
    
    // Dark background matching Disco Elysium aesthetic
    VkClearValue clearColor = {{{0.05f, 0.05f, 0.07f, 1.0f}}};
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Call custom render callback (for text rendering)
    if (get_render_callback()) {
        get_render_callback()(cmd);
    }
    
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
    
    // Submit
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSems[] = {e->imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSems;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VkSemaphore signalSems[] = {e->renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSems;
    
    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, e->inFlightFence);
    
    // Present
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSems;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    vkQueuePresentKHR(e->presentQueue, &presentInfo);
}

// =============================================================================
// Event Handling
// =============================================================================

inline bool should_close() {
    auto* e = get_engine();
    return !e || !e->running;
}

// =============================================================================
// Polling-based Event Queue (for jank which can't use callbacks)
// =============================================================================

struct InputEvent {
    int type;       // 0=none, 1=key_down, 2=key_up, 3=scroll, 4=mouse_motion, 5=mouse_down, 6=mouse_up
    int scancode;   // For key events
    float scrollY;  // For scroll events
    float mouseX;   // For mouse events
    float mouseY;   // For mouse events
    int mouseButton; // For mouse click events (1=left, 2=middle, 3=right)
};

inline std::vector<InputEvent>& get_event_queue() {
    static std::vector<InputEvent> queue;
    return queue;
}

// Key event callback (kept for C++ use)
typedef void (*KeyCallback)(int scancode, bool pressed);
inline KeyCallback& get_key_callback() {
    static KeyCallback cb = nullptr;
    return cb;
}
inline void set_key_callback(KeyCallback cb) { get_key_callback() = cb; }

// Scroll callback (kept for C++ use)
typedef void (*ScrollCallback)(float deltaY);
inline ScrollCallback& get_scroll_callback() {
    static ScrollCallback cb = nullptr;
    return cb;
}
inline void set_scroll_callback(ScrollCallback cb) { get_scroll_callback() = cb; }

inline void poll_events() {
    auto* e = get_engine();
    if (!e) return;
    
    // Clear previous frame's events
    get_event_queue().clear();
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                e->running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    e->running = false;
                } else {
                    // Add to event queue for jank polling
                    get_event_queue().push_back({1, (int)event.key.scancode, 0.0f, 0.0f, 0.0f, 0});
                    // Also call callback if set
                    if (get_key_callback()) {
                        get_key_callback()(event.key.scancode, true);
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                get_event_queue().push_back({2, (int)event.key.scancode, 0.0f, 0.0f, 0.0f, 0});
                if (get_key_callback()) {
                    get_key_callback()(event.key.scancode, false);
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                get_event_queue().push_back({3, 0, event.wheel.y, 0.0f, 0.0f, 0});
                if (get_scroll_callback()) {
                    get_scroll_callback()(event.wheel.y);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                get_event_queue().push_back({4, 0, 0.0f, event.motion.x, event.motion.y, 0});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                get_event_queue().push_back({5, 0, 0.0f, event.button.x, event.button.y, (int)event.button.button});
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                get_event_queue().push_back({6, 0, 0.0f, event.button.x, event.button.y, (int)event.button.button});
                break;
        }
    }
}

// Get number of events in queue
inline int get_event_count() {
    return static_cast<int>(get_event_queue().size());
}

// Get event at index (returns type, or 0 if out of range)
inline int get_event_type(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].type;
}

inline int get_event_scancode(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].scancode;
}

inline float get_event_scroll_y(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].scrollY;
}

inline float get_event_mouse_x(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].mouseX;
}

inline float get_event_mouse_y(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0.0f;
    return q[index].mouseY;
}

inline int get_event_mouse_button(int index) {
    auto& q = get_event_queue();
    if (index < 0 || index >= (int)q.size()) return 0;
    return q[index].mouseButton;
}

// =============================================================================
// Accessors for text renderer initialization
// =============================================================================

inline VkDevice get_device() {
    auto* e = get_engine();
    return e ? e->device : VK_NULL_HANDLE;
}

inline VkPhysicalDevice get_physical_device() {
    auto* e = get_engine();
    return e ? e->physicalDevice : VK_NULL_HANDLE;
}

inline VkQueue get_graphics_queue() {
    auto* e = get_engine();
    return e ? e->graphicsQueue : VK_NULL_HANDLE;
}

inline VkCommandPool get_command_pool() {
    auto* e = get_engine();
    return e ? e->commandPool : VK_NULL_HANDLE;
}

inline VkRenderPass get_render_pass() {
    auto* e = get_engine();
    return e ? e->renderPass : VK_NULL_HANDLE;
}

inline uint32_t get_screen_width() {
    auto* e = get_engine();
    return e ? e->framebufferWidth : 0;
}

inline uint32_t get_screen_height() {
    auto* e = get_engine();
    return e ? e->framebufferHeight : 0;
}

inline float get_pixel_scale() {
    auto* e = get_engine();
    if (!e || !e->window) return 1.0f;
    int winW, winH;
    SDL_GetWindowSize(e->window, &winW, &winH);
    if (winW <= 0) return 1.0f;
    return (float)e->framebufferWidth / (float)winW;
}

// =============================================================================
// File I/O (since jank slurp has bugs)
// =============================================================================

// Returns file content as lines, separated by null chars, total count in first position
// Format: "count\0line1\0line2\0line3\0..."
inline const char* read_text_file(const char* filepath) {
    static std::string content;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[fiction] Failed to open file: " << filepath << std::endl;
        content = "";
        return content.c_str();
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return content.c_str();
}

// Read file and return as vector of lines (C++ does the splitting)
inline int read_file_line_count(const char* filepath) {
    static std::vector<std::string> lines;
    lines.clear();
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return 0;
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return static_cast<int>(lines.size());
}

inline std::vector<std::string>& get_file_lines() {
    static std::vector<std::string> lines;
    return lines;
}

inline int read_file_lines(const char* filepath) {
    auto& lines = get_file_lines();
    lines.clear();
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[fiction] Failed to open file: " << filepath << std::endl;
        return 0;
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return static_cast<int>(lines.size());
}

inline const char* get_file_line(int index) {
    auto& lines = get_file_lines();
    if (index < 0 || index >= static_cast<int>(lines.size())) {
        return "";
    }
    return lines[index].c_str();
}

// File modification time (for hot-reloading)
inline int64_t get_file_mod_time(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_mtime);
    }
    return 0;
}

} // namespace fiction_engine
