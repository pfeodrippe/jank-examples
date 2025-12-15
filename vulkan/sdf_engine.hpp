// SDF Engine - Jank-callable wrapper for Vulkan SDF renderer
// Uses ODR-safe heap-pointer pattern for JIT compatibility
// SDL3 version with event recording/replay for automated testing

#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

// ImGui
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include <cmath>
#include <chrono>
#include <set>
#include <sys/stat.h>
#include <csignal>
#include <shaderc/shaderc.hpp>
#include <dirent.h>
#include <algorithm>

// stb_image_write implementation is in stb_impl.o
#include "stb_image_write.h"

// CPU Marching Cubes for GPU-sampled SDF grids (standalone, no dependencies)
#include "marching_cubes.hpp"

namespace sdfx {

// SIGINT handler for Ctrl+C
inline bool g_sigint_received = false;
inline void sigint_handler(int sig) {
    (void)sig;
    g_sigint_received = true;
    std::cout << "\nCtrl+C received, shutting down..." << std::endl;
}

const uint32_t WINDOW_WIDTH = 1280;
const uint32_t WINDOW_HEIGHT = 720;

// Actual framebuffer size (set at runtime, may be 2x on Retina)
inline uint32_t g_framebufferWidth = WINDOW_WIDTH;
inline uint32_t g_framebufferHeight = WINDOW_HEIGHT;

const int MAX_FRAMES_IN_FLIGHT = 2;
const int MAX_OBJECTS = 32;  // Maximum objects supported in shader

// Extensible scene object with position and rotation
struct SceneObject {
    float position[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};  // Euler angles in radians
    int type = 0;  // Object type for shader
    bool selectable = true;

    SceneObject() = default;
    SceneObject(float x, float y, float z, int objType = 0, bool sel = true)
        : type(objType), selectable(sel) {
        position[0] = x; position[1] = y; position[2] = z;
    }
};

struct UBO {
    float cameraPos[4];
    float cameraTarget[4];
    float lightDir[4];
    float resolution[4];
    // Mesh preview options
    float options[4];       // x=useVertexColors (for mesh preview), y/z/w=unused
    // Edit mode uniforms
    float editMode[4];      // x=enabled, y=selectedObject, z=hoveredAxis, w=objectCount
    float gizmoPos[4];      // xyz=position of gizmo, w=unused
    float gizmoRot[4];      // xyz=rotation of selected object (for preview), w=unused
    // Object transforms (extensible arrays)
    float objPositions[MAX_OBJECTS][4];  // xyz=position, w=type
    float objRotations[MAX_OBJECTS][4];  // xyz=Euler angles, w=unused
};

struct Camera {
    float distance = 8.0f;
    float angleX = 0.3f;
    float angleY = 0.0f;
    float targetX = 0.0f;  // Pan X
    float targetY = 0.0f;  // Pan Y (vertical)
    float targetZ = 0.0f;  // Pan Z

    void update(float dx, float dy, float dscroll) {
        angleY += dx * 0.01f;
        angleX += dy * 0.01f;
        angleX = std::max(-1.5f, std::min(1.5f, angleX));
        distance -= dscroll * 0.5f;
        distance = std::max(0.3f, std::min(50.0f, distance));
    }

    // Pan camera in screen-space (right/up relative to view)
    void pan(float dx, float dy) {
        // Calculate screen-space right and up vectors based on camera orientation
        float speed = distance * 0.002f;

        // Right vector in world space (perpendicular to view direction in XZ plane)
        float rightX = cos(angleY);
        float rightZ = -sin(angleY);

        // Up is just Y (we don't tilt)
        targetX += rightX * dx * speed;
        targetZ += rightZ * dx * speed;
        targetY -= dy * speed;  // Y is inverted in screen space
    }

    void getPosition(float* pos) const {
        pos[0] = distance * cos(angleX) * sin(angleY) + targetX;
        pos[1] = distance * sin(angleX) + targetY;
        pos[2] = distance * cos(angleX) * cos(angleY) + targetZ;
    }

    void getTarget(float* target) const {
        target[0] = targetX;
        target[1] = targetY;
        target[2] = targetZ;
    }
};

struct Engine {
    SDL_Window* window = nullptr;
    bool running = true;
    Camera camera;
    bool mousePressed = false;
    bool rightMousePressed = false;  // For panning
    float lastMouseX = 0, lastMouseY = 0;

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

    VkImage computeImage = VK_NULL_HANDLE;
    VkDeviceMemory computeImageMemory = VK_NULL_HANDLE;
    VkImageView computeImageView = VK_NULL_HANDLE;

    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
    void* uniformMapped = nullptr;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout blitDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool blitDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet blitDescriptorSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // ImGui
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    bool showPropertiesPanel = true;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    uint32_t currentImageIndex = 0;  // For viewport screenshot

    float time = 0.0f;
    bool initialized = false;
    bool dirty = true;  // Set true when scene needs re-rendering
    bool continuousMode = false;  // When true, always re-render (for animations)
    std::string shaderDir;

    // Shader switching
    std::vector<std::string> shaderList;  // List of .comp files
    int currentShaderIndex = 0;           // Currently loaded shader
    std::string currentShaderName;        // Name of current shader (without path/extension)
    time_t lastShaderModTime = 0;

    // Edit mode state
    bool editMode = false;
    int selectedObject = -1;  // Material ID of selected object (-1 = none)
    int hoveredAxis = -1;     // 0=X, 1=Y, 2=Z, -1=none
    int draggingAxis = -1;    // Which axis is being dragged
    float dragStartPos[3] = {0, 0, 0};  // World position where drag started
    float selectedPos[3] = {0, 0, 0};   // Position of selected object (gizmo follows this)

    // Extensible object list
    std::vector<SceneObject> objects;

    // Selected object transform (for gizmo feedback during drag)
    float selectedRot[3] = {0, 0, 0};

    // Keyboard requests (consumed by Jank)
    bool undoRequested = false;
    bool redoRequested = false;
    bool duplicateRequested = false;
    bool deleteRequested = false;
    bool resetTransformRequested = false;
    int pendingShaderSwitch = 0;  // 0=none, 1=next, -1=previous (consumed by Jank)

    // Temporary SPIR-V storage for jank-orchestrated pipeline creation
    std::vector<uint32_t> pendingSpirvData;

    // Depth buffer for solid mesh rendering
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    // Mesh preview state
    bool meshPreviewVisible = false;
    bool meshRenderSolid = true;  // true = solid, false = wireframe
    bool meshUseVertexColors = true;  // true = use sampled vertex colors (default on)
    bool meshUseDualContouring = true;  // true = use DC (sharper features), false = marching cubes
    float meshScale = 1.0f;       // Scale factor for mesh preview
    int meshPreviewResolution = 256;
    VkBuffer meshVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory meshVertexMemory = VK_NULL_HANDLE;
    VkBuffer meshIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory meshIndexMemory = VK_NULL_HANDLE;
    uint32_t meshIndexCount = 0;
    uint32_t meshVertexCount = 0;
    VkPipeline meshPipeline = VK_NULL_HANDLE;
    VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout meshDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool meshDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet meshDescriptorSet = VK_NULL_HANDLE;
    bool meshPipelineInitialized = false;
    bool meshNeedsRegenerate = false;

    // Stored mesh for export (shared between preview and export)
    mc::Mesh currentMesh;
    int currentMeshResolution = 0;  // Resolution at which currentMesh was generated

    // Initialize default scene objects
    void initDefaultScene() {
        objects.clear();
        objects.push_back(SceneObject(0.0f, 0.0f, 0.0f, 0, false));   // 0 = ground (not selectable)
        objects.push_back(SceneObject(0.0f, 0.5f, 0.0f, 1, true));    // 1 = blob
        objects.push_back(SceneObject(3.0f, 0.3f, 0.0f, 2, true));    // 2 = torus
        objects.push_back(SceneObject(-3.0f, 0.5f, 0.0f, 3, true));   // 3 = carved box
        objects.push_back(SceneObject(0.0f, 0.0f, 3.0f, 4, true));    // 4 = twisted column
        objects.push_back(SceneObject(0.0f, 0.3f, -3.0f, 5, true));   // 5 = repeated spheres
    }

    // Add a new object to the scene
    int addObject(float x, float y, float z, int type = 0) {
        if (objects.size() >= MAX_OBJECTS) return -1;
        objects.push_back(SceneObject(x, y, z, type, true));
        return (int)objects.size() - 1;
    }

    // Get object position (bounds-checked)
    float* getObjectPosition(int idx) {
        if (idx >= 0 && idx < (int)objects.size()) {
            return objects[idx].position;
        }
        return nullptr;
    }

    // Get object rotation (bounds-checked)
    float* getObjectRotation(int idx) {
        if (idx >= 0 && idx < (int)objects.size()) {
            return objects[idx].rotation;
        }
        return nullptr;
    }
};

// ODR-safe global state using function-local static
// This pattern ensures the same Engine* is shared across all translation units
// (AOT and JIT) because inline functions with function-local statics have
// vague linkage in C++17 - they are merged across TUs.
inline Engine*& get_engine() {
    static Engine* ptr = nullptr;
    return ptr;
}


// Ray-gizmo intersection for axis picking
inline int raycast_gizmo(Engine* e, double mouseX, double mouseY) {
    // Scale mouse coordinates from window space to framebuffer space (for Retina displays)
    float scaleX = (float)g_framebufferWidth / WINDOW_WIDTH;
    float scaleY = (float)g_framebufferHeight / WINDOW_HEIGHT;
    float fbMouseX = (float)mouseX * scaleX;
    float fbMouseY = (float)mouseY * scaleY;

    // Match shader's UV calculation
    float uvX = (2.0f * fbMouseX - g_framebufferWidth) / g_framebufferHeight;
    float uvY = (2.0f * fbMouseY - g_framebufferHeight) / g_framebufferHeight;

    // Get camera position
    float camPos[3];
    e->camera.getPosition(camPos);

    // Camera target
    float target[3];
    e->camera.getTarget(target);

    // Match shader's setCamera() exactly:
    // vec3 cw = normalize(ta - ro);
    // vec3 up = vec3(0, -1, 0);
    // vec3 cu = normalize(cross(cw, up));
    // vec3 cv = cross(cu, cw);

    float cw[3] = {
        target[0] - camPos[0],
        target[1] - camPos[1],
        target[2] - camPos[2]
    };
    float cwLen = sqrt(cw[0]*cw[0] + cw[1]*cw[1] + cw[2]*cw[2]);
    cw[0] /= cwLen; cw[1] /= cwLen; cw[2] /= cwLen;

    float up[3] = {0, -1, 0};

    // cu = normalize(cross(cw, up))
    float cu[3] = {
        cw[1]*up[2] - cw[2]*up[1],
        cw[2]*up[0] - cw[0]*up[2],
        cw[0]*up[1] - cw[1]*up[0]
    };
    float cuLen = sqrt(cu[0]*cu[0] + cu[1]*cu[1] + cu[2]*cu[2]);
    cu[0] /= cuLen; cu[1] /= cuLen; cu[2] /= cuLen;

    // cv = cross(cu, cw)
    float cv[3] = {
        cu[1]*cw[2] - cu[2]*cw[1],
        cu[2]*cw[0] - cu[0]*cw[2],
        cu[0]*cw[1] - cu[1]*cw[0]
    };

    // Ray direction: ca * normalize(vec3(uv, fov))
    // ca is mat3(cu, cv, cw) - columns are cu, cv, cw
    float fov = 1.5f;
    float localDir[3] = {uvX, uvY, fov};
    float localLen = sqrt(localDir[0]*localDir[0] + localDir[1]*localDir[1] + localDir[2]*localDir[2]);
    localDir[0] /= localLen; localDir[1] /= localLen; localDir[2] /= localLen;

    // Multiply by camera matrix (columns are cu, cv, cw)
    float rayDir[3] = {
        cu[0]*localDir[0] + cv[0]*localDir[1] + cw[0]*localDir[2],
        cu[1]*localDir[0] + cv[1]*localDir[1] + cw[1]*localDir[2],
        cu[2]*localDir[0] + cv[2]*localDir[1] + cw[2]*localDir[2]
    };

    // Gizmo center
    float gizmoCenter[3] = {e->selectedPos[0], e->selectedPos[1], e->selectedPos[2]};

    // Simple axis hit test - check distance to each axis line
    float axisLen = 0.8f;
    float hitThreshold = 0.15f;  // How close ray needs to be to axis

    int bestAxis = -1;
    float bestDist = 1000.0f;

    // For each axis, find closest point on ray to axis line
    for (int axis = 0; axis < 3; axis++) {
        // Axis direction
        float axisDir[3] = {0, 0, 0};
        axisDir[axis] = 1.0f;

        // Axis line: gizmoCenter + t * axisDir, t in [0, axisLen]
        // Ray: camPos + s * rayDir

        // Find closest points between the two lines
        // Using the formula for closest point between two lines
        float w0[3] = {
            camPos[0] - gizmoCenter[0],
            camPos[1] - gizmoCenter[1],
            camPos[2] - gizmoCenter[2]
        };

        float a = rayDir[0]*rayDir[0] + rayDir[1]*rayDir[1] + rayDir[2]*rayDir[2]; // = 1
        float b = rayDir[0]*axisDir[0] + rayDir[1]*axisDir[1] + rayDir[2]*axisDir[2];
        float c = axisDir[0]*axisDir[0] + axisDir[1]*axisDir[1] + axisDir[2]*axisDir[2]; // = 1
        float d = rayDir[0]*w0[0] + rayDir[1]*w0[1] + rayDir[2]*w0[2];
        float ee = axisDir[0]*w0[0] + axisDir[1]*w0[1] + axisDir[2]*w0[2];

        float denom = a*c - b*b;
        if (fabs(denom) < 0.0001f) continue;  // Lines parallel

        float s = (b*ee - c*d) / denom;  // Parameter on ray
        float t = (a*ee - b*d) / denom;  // Parameter on axis

        // Clamp t to axis length
        t = std::max(0.0f, std::min(axisLen, t));

        // Only consider if ray parameter is positive (in front of camera)
        if (s < 0) continue;

        // Points on each line
        float p1[3] = {camPos[0] + s*rayDir[0], camPos[1] + s*rayDir[1], camPos[2] + s*rayDir[2]};
        float p2[3] = {
            gizmoCenter[0] + t*axisDir[0],
            gizmoCenter[1] + t*axisDir[1],
            gizmoCenter[2] + t*axisDir[2]
        };

        // Distance between closest points
        float dist = sqrt(
            (p1[0]-p2[0])*(p1[0]-p2[0]) +
            (p1[1]-p2[1])*(p1[1]-p2[1]) +
            (p1[2]-p2[2])*(p1[2]-p2[2])
        );

        if (dist < hitThreshold && dist < bestDist) {
            bestDist = dist;
            bestAxis = axis;
        }
    }

    // Check rotation rings (torus shapes)
    // For each ring, find where ray intersects the plane, then check if at ring radius
    float ringRadius = 0.8f;
    float ringThick = 0.1f;  // Hit tolerance for ring

    for (int ring = 0; ring < 3; ring++) {
        // Ring normal (perpendicular to the ring plane)
        float normal[3] = {0, 0, 0};
        normal[ring] = 1.0f;

        // Ray-plane intersection: t = (planePoint - rayOrigin) . normal / (rayDir . normal)
        float denom = rayDir[0]*normal[0] + rayDir[1]*normal[1] + rayDir[2]*normal[2];
        if (fabs(denom) < 0.0001f) continue;  // Ray parallel to plane

        float t = ((gizmoCenter[0] - camPos[0])*normal[0] +
                   (gizmoCenter[1] - camPos[1])*normal[1] +
                   (gizmoCenter[2] - camPos[2])*normal[2]) / denom;

        if (t < 0) continue;  // Behind camera

        // Point on plane
        float hitPoint[3] = {
            camPos[0] + t*rayDir[0],
            camPos[1] + t*rayDir[1],
            camPos[2] + t*rayDir[2]
        };

        // Distance from gizmo center in the ring plane
        float dx = hitPoint[0] - gizmoCenter[0];
        float dy = hitPoint[1] - gizmoCenter[1];
        float dz = hitPoint[2] - gizmoCenter[2];

        // Distance in the plane of the ring (exclude the normal component)
        float distInPlane;
        if (ring == 0) {  // X ring - YZ plane
            distInPlane = sqrt(dy*dy + dz*dz);
        } else if (ring == 1) {  // Y ring - XZ plane
            distInPlane = sqrt(dx*dx + dz*dz);
        } else {  // Z ring - XY plane
            distInPlane = sqrt(dx*dx + dy*dy);
        }

        // Check if hit is on the ring (close to ringRadius)
        float distFromRing = fabs(distInPlane - ringRadius);
        if (distFromRing < ringThick && distFromRing < bestDist) {
            bestDist = distFromRing;
            bestAxis = 3 + ring;  // 3=rotX, 4=rotY, 5=rotZ
        }
    }

    return bestAxis;
}

// SDL3 Event Handlers
inline void handle_mouse_button(Uint8 button, bool pressed, float x, float y) {
    auto* e = get_engine();
    if (!e) return;

    if (button == SDL_BUTTON_LEFT) {
        e->mousePressed = pressed;
        e->lastMouseX = x;
        e->lastMouseY = y;

        // Handle gizmo dragging in edit mode
        if (e->editMode && e->selectedObject >= 0) {
            if (pressed) {
                // Check if clicking on a gizmo axis
                int clickedAxis = raycast_gizmo(e, x, y);
                if (clickedAxis >= 0) {
                    e->draggingAxis = clickedAxis;
                    e->dragStartPos[0] = e->selectedPos[0];
                    e->dragStartPos[1] = e->selectedPos[1];
                    e->dragStartPos[2] = e->selectedPos[2];
                    const char* axisNames[] = {"X (red)", "Y (green)", "Z (blue)", "Rot-X", "Rot-Y", "Rot-Z"};
                    std::cout << "Dragging " << axisNames[clickedAxis] << std::endl;
                }
            } else {
                // End drag
                if (e->draggingAxis >= 0) {
                    std::cout << "New position: ("
                              << e->selectedPos[0] << ", "
                              << e->selectedPos[1] << ", "
                              << e->selectedPos[2] << ")" << std::endl;
                }
                e->draggingAxis = -1;
            }
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        // Right click for panning
        e->rightMousePressed = pressed;
        e->lastMouseX = x;
        e->lastMouseY = y;
    }
}

inline void handle_mouse_motion(float x, float y, float xrel, float yrel) {
    auto* e = get_engine();
    if (!e) return;

    // Update hover state when in edit mode
    if (e->editMode && e->selectedObject >= 0) {
        int newHoveredAxis;
        if (e->draggingAxis >= 0) {
            // While dragging, keep the dragged axis highlighted
            newHoveredAxis = e->draggingAxis;
        } else {
            // When not dragging, raycast to find hovered axis
            newHoveredAxis = raycast_gizmo(e, x, y);
        }
        // Only mark dirty if hover state changed
        if (newHoveredAxis != e->hoveredAxis) {
            e->hoveredAxis = newHoveredAxis;
            e->dirty = true;
        }
    }

    if (!e->mousePressed && !e->rightMousePressed) {
        e->lastMouseX = x;
        e->lastMouseY = y;
        return;
    }

    float dx = xrel;
    float dy = yrel;

    // Right-click drag = pan camera
    if (e->rightMousePressed) {
        e->camera.pan(dx, dy);
        e->dirty = true;
        e->lastMouseX = x;
        e->lastMouseY = y;
        return;
    }

    // Handle gizmo dragging - project mouse movement onto axis in screen space
    if (e->editMode && e->draggingAxis >= 0 && e->selectedObject >= 0 && e->selectedObject < (int)e->objects.size()) {

        // Check if this is a rotation ring (axes 3, 4, 5) or translation axis (0, 1, 2)
        bool isRotation = (e->draggingAxis >= 3 && e->draggingAxis <= 5);
        int axisIndex = isRotation ? (e->draggingAxis - 3) : e->draggingAxis;

        float speed = isRotation ? 0.01f : (0.01f * e->camera.distance);

        // Get camera basis vectors (same as in raycast_gizmo)
        float camPos[3];
        e->camera.getPosition(camPos);
        float target[3];
    e->camera.getTarget(target);

        float cw[3] = {target[0] - camPos[0], target[1] - camPos[1], target[2] - camPos[2]};
        float cwLen = sqrt(cw[0]*cw[0] + cw[1]*cw[1] + cw[2]*cw[2]);
        cw[0] /= cwLen; cw[1] /= cwLen; cw[2] /= cwLen;

        float up[3] = {0, -1, 0};
        float cu[3] = {cw[1]*up[2] - cw[2]*up[1], cw[2]*up[0] - cw[0]*up[2], cw[0]*up[1] - cw[1]*up[0]};
        float cuLen = sqrt(cu[0]*cu[0] + cu[1]*cu[1] + cu[2]*cu[2]);
        cu[0] /= cuLen; cu[1] /= cuLen; cu[2] /= cuLen;

        float cv[3] = {cu[1]*cw[2] - cu[2]*cw[1], cu[2]*cw[0] - cu[0]*cw[2], cu[0]*cw[1] - cu[1]*cw[0]};

        if (isRotation) {
            // Camera-relative rotation
            // Get the rotation axis direction
            float axisDir[3] = {0, 0, 0};
            axisDir[axisIndex] = 1.0f;

            // Project axis onto screen (how axis appears in screen space)
            float screenX = axisDir[0]*cu[0] + axisDir[1]*cu[1] + axisDir[2]*cu[2];
            float screenY = axisDir[0]*cv[0] + axisDir[1]*cv[1] + axisDir[2]*cv[2];

            // Mouse movement perpendicular to projected axis drives rotation
            // Use cross product in 2D: perpendicular to (screenX, screenY) is (-screenY, screenX)
            float rotation = (-dy * screenX + dx * screenY) * speed;

            // Determine sign based on which side of the rotation plane the camera is on
            // Vector from object to camera
            float objToCamera[3] = {
                camPos[0] - e->selectedPos[0],
                camPos[1] - e->selectedPos[1],
                camPos[2] - e->selectedPos[2]
            };

            // If camera is on negative side of the plane, flip rotation direction
            if (objToCamera[axisIndex] < 0) rotation = -rotation;

            e->selectedRot[axisIndex] += rotation;
            e->objects[e->selectedObject].rotation[axisIndex] = e->selectedRot[axisIndex];
            e->dirty = true;
        } else {
            // Translation: project axis onto screen
            float axisDir[3] = {0, 0, 0};
            axisDir[axisIndex] = 1.0f;

            // Project axis onto screen (dot with camera right and up)
            float screenX = axisDir[0]*cu[0] + axisDir[1]*cu[1] + axisDir[2]*cu[2];
            float screenY = axisDir[0]*cv[0] + axisDir[1]*cv[1] + axisDir[2]*cv[2];

            // Mouse delta dotted with axis screen direction gives movement amount
            float movement = (dx * screenX - dy * screenY) * speed;

            // Invert Y axis (green) direction
            if (axisIndex == 1) movement = -movement;

            e->selectedPos[axisIndex] += movement;
            e->objects[e->selectedObject].position[0] = e->selectedPos[0];
            e->objects[e->selectedObject].position[1] = e->selectedPos[1];
            e->objects[e->selectedObject].position[2] = e->selectedPos[2];
            e->dirty = true;
        }
    } else {
        // Normal camera orbit
        e->camera.update(dx, dy, 0);
        e->dirty = true;
    }

    e->lastMouseX = x;
    e->lastMouseY = y;
}

// Forward declarations
inline std::vector<char> read_file(const std::string& filename);
inline std::string read_text_file(const std::string& filename);

inline void select_object(Engine* e, int id) {
    if (id >= 0 && id < (int)e->objects.size() && e->objects[id].selectable) {
        e->selectedObject = id;
        // Load position and rotation from Engine's objects vector
        e->selectedPos[0] = e->objects[id].position[0];
        e->selectedPos[1] = e->objects[id].position[1];
        e->selectedPos[2] = e->objects[id].position[2];
        e->selectedRot[0] = e->objects[id].rotation[0];
        e->selectedRot[1] = e->objects[id].rotation[1];
        e->selectedRot[2] = e->objects[id].rotation[2];
        e->dirty = true;
        std::cout << "Selected object " << id << " at ("
                  << e->selectedPos[0] << ", "
                  << e->selectedPos[1] << ", "
                  << e->selectedPos[2] << ")" << std::endl;
    }
}

// Forward declarations for shader switching
inline void scan_shaders();
inline void load_shader_by_name(const std::string& name);
inline std::vector<uint32_t> compile_glsl_to_spirv(const std::string& source,
                                                    const std::string& filename,
                                                    shaderc_shader_kind kind);

// ============================================================================
// Shader switching functions
// ============================================================================

inline void scan_shaders() {
    auto* e = get_engine();
    if (!e) return;

    e->shaderList.clear();
    DIR* dir = opendir(e->shaderDir.c_str());
    if (!dir) {
        std::cerr << "Could not open shader directory: " << e->shaderDir << std::endl;
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Check for .comp extension
        if (name.size() > 5 && name.substr(name.size() - 5) == ".comp") {
            // Remove .comp extension
            std::string shaderName = name.substr(0, name.size() - 5);
            e->shaderList.push_back(shaderName);
        }
    }
    closedir(dir);

    // Sort shader list alphabetically
    std::sort(e->shaderList.begin(), e->shaderList.end());

    // Find current shader index
    for (size_t i = 0; i < e->shaderList.size(); i++) {
        if (e->shaderList[i] == e->currentShaderName) {
            e->currentShaderIndex = static_cast<int>(i);
            break;
        }
    }

    std::cout << "Found " << e->shaderList.size() << " shaders:" << std::endl;
    for (size_t i = 0; i < e->shaderList.size(); i++) {
        std::cout << "  [" << i << "] " << e->shaderList[i];
        if (e->shaderList[i] == e->currentShaderName) std::cout << " (current)";
        std::cout << std::endl;
    }
}

inline void load_shader_by_name(const std::string& name) {
    std::cout << "[DEBUG] load_shader_by_name: entering with name=" << name << std::endl;
    auto* e = get_engine();
    if (!e) {
        std::cerr << "[DEBUG] load_shader_by_name: engine is null!" << std::endl;
        return;
    }
    if (!e->initialized) {
        std::cerr << "[DEBUG] load_shader_by_name: engine not initialized!" << std::endl;
        return;
    }
    std::cout << "[DEBUG] load_shader_by_name: engine OK, shaderDir=" << e->shaderDir << std::endl;

    vkDeviceWaitIdle(e->device);

    std::string compPath = e->shaderDir + "/" + name + ".comp";
    std::cout << "[DEBUG] load_shader_by_name: compPath=" << compPath << std::endl;

    // Read GLSL source
    std::string glslSource = read_text_file(compPath);
    if (glslSource.empty()) {
        std::cerr << "[DEBUG] load_shader_by_name: Failed to read shader source: " << compPath << std::endl;
        return;
    }

    // Compile GLSL to SPIR-V using bundled shaderc library (no external glslangValidator needed)
    std::cout << "[DEBUG] load_shader_by_name: compiling with shaderc..." << std::endl;
    auto spirv = compile_glsl_to_spirv(glslSource, name + ".comp", shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "[DEBUG] load_shader_by_name: compile FAILED!" << std::endl;
        return;
    }
    std::cout << "[DEBUG] load_shader_by_name: compile SUCCESS (" << spirv.size() << " words)" << std::endl;

    // Destroy old pipeline and shader module
    vkDestroyPipeline(e->device, e->computePipeline, nullptr);
    vkDestroyPipelineLayout(e->device, e->computePipelineLayout, nullptr);
    vkDestroyShaderModule(e->device, e->computeShaderModule, nullptr);

    // Create shader module from in-memory SPIR-V
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    if (vkCreateShaderModule(e->device, &createInfo, nullptr, &e->computeShaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create shader module" << std::endl;
        return;
    }

    // Create compute pipeline
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->descriptorSetLayout;
    vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->computePipelineLayout);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = e->computeShaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = e->computePipelineLayout;
    VkResult pipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &e->computePipeline);
    if (pipelineResult != VK_SUCCESS) {
        std::cerr << "[DEBUG] load_shader_by_name: vkCreateComputePipelines FAILED! result=" << pipelineResult << std::endl;
        return;
    }
    std::cout << "[DEBUG] load_shader_by_name: pipeline created successfully, handle=" << (void*)e->computePipeline << std::endl;

    e->currentShaderName = name;
    e->dirty = true;
    std::cout << "[DEBUG] load_shader_by_name: dirty flag set to TRUE" << std::endl;

    // Update modification time for auto-reload
    std::string shaderPath = e->shaderDir + "/" + name + ".comp";
    struct stat st;
    if (stat(shaderPath.c_str(), &st) == 0) {
        e->lastShaderModTime = st.st_mtime;
    }

    std::cout << "[DEBUG] load_shader_by_name: COMPLETE! Loaded shader: " << name << std::endl;
}

inline uint32_t find_memory_type(Engine* e, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(e->physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

inline std::vector<char> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return {};
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

// Read GLSL source file as text string
inline std::string read_text_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Compile GLSL source to SPIR-V in memory using shaderc
// Returns empty vector on failure
inline std::vector<uint32_t> compile_glsl_to_spirv(const std::string& source,
                                                    const std::string& filename,
                                                    shaderc_shader_kind kind) {
    static shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    auto result = compiler.CompileGlslToSpv(source, kind, filename.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader compilation error:\n" << result.GetErrorMessage() << std::endl;
        return {};
    }

    return std::vector<uint32_t>(result.cbegin(), result.cend());
}

// Main API functions
inline bool init(const char* shader_dir) {
    if (get_engine() && get_engine()->initialized) {
        std::cout << "Already initialized" << std::endl;
        return true;
    }

    get_engine() = new Engine();
    auto* e = get_engine();
    e->shaderDir = shader_dir;

    // Set up SIGINT handler for Ctrl+C
    g_sigint_received = false;
    std::signal(SIGINT, sigint_handler);

    // Init SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    e->window = SDL_CreateWindow(
        "SDF Jank",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!e->window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Get actual framebuffer size (may be 2x on Retina displays)
    int fbWidth, fbHeight;
    SDL_GetWindowSizeInPixels(e->window, &fbWidth, &fbHeight);
    g_framebufferWidth = (uint32_t)fbWidth;
    g_framebufferHeight = (uint32_t)fbHeight;
    std::cout << "Framebuffer size: " << g_framebufferWidth << "x" << g_framebufferHeight << std::endl;

    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SDF Jank";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Jank";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions;
    for (uint32_t i = 0; i < sdlExtCount; i++) {
        extensions.push_back(sdlExts[i]);
    }
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
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
    std::cout << "GPU: " << props.deviceName << std::endl;

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
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(qci);
    }

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_KHR_portability_subset"
    };

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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
    e->swapchainExtent = {g_framebufferWidth, g_framebufferHeight};

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = e->surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = e->swapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = e->swapchainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

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

    e->swapchainImageViews.resize(e->swapchainImages.size());
    for (size_t i = 0; i < e->swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
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

    // Create compute image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {g_framebufferWidth, g_framebufferHeight, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(e->device, &imageInfo, nullptr, &e->computeImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(e->device, e->computeImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(e->device, &allocInfo, nullptr, &e->computeImageMemory);
    vkBindImageMemory(e->device, e->computeImage, e->computeImageMemory, 0);

    VkImageViewCreateInfo computeViewInfo{};
    computeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    computeViewInfo.image = e->computeImage;
    computeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    computeViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    computeViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCreateImageView(e->device, &computeViewInfo, nullptr, &e->computeImageView);

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    vkCreateSampler(e->device, &samplerInfo, nullptr, &e->sampler);

    // Create uniform buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(UBO);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(e->device, &bufferInfo, nullptr, &e->uniformBuffer);

    VkMemoryRequirements bufMemReq;
    vkGetBufferMemoryRequirements(e->device, e->uniformBuffer, &bufMemReq);

    VkMemoryAllocateInfo bufAllocInfo{};
    bufAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufAllocInfo.allocationSize = bufMemReq.size;
    bufAllocInfo.memoryTypeIndex = find_memory_type(e, bufMemReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(e->device, &bufAllocInfo, nullptr, &e->uniformMemory);
    vkBindBufferMemory(e->device, e->uniformBuffer, e->uniformMemory, 0);
    vkMapMemory(e->device, e->uniformMemory, 0, sizeof(UBO), 0, &e->uniformMapped);

    // Descriptor set layouts
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(e->device, &layoutInfo, nullptr, &e->descriptorSetLayout);

    VkDescriptorSetLayoutBinding samplerBinding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo blitLayoutInfo{};
    blitLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    blitLayoutInfo.bindingCount = 1;
    blitLayoutInfo.pBindings = &samplerBinding;

    vkCreateDescriptorSetLayout(e->device, &blitLayoutInfo, nullptr, &e->blitDescriptorSetLayout);

    // Set default shader name BEFORE loading
    e->currentShaderName = "hand_cigarette";  // Default shader name (without .comp extension)

    // Load and compile compute shader in memory (no .spv file needed!)
    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    std::string glslSource = read_text_file(shaderPath);
    if (glslSource.empty()) {
        std::cerr << "Failed to load shader source: " << shaderPath << std::endl;
        return false;
    }

    auto spirv = compile_glsl_to_spirv(glslSource, (e->currentShaderName + ".comp").c_str(), shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "Shader compilation failed!" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderModuleInfo.pCode = spirv.data();

    if (vkCreateShaderModule(e->device, &shaderModuleInfo, nullptr, &e->computeShaderModule) != VK_SUCCESS) {
        std::cerr << "Shader module creation failed" << std::endl;
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = e->computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &e->descriptorSetLayout;

    vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->computePipelineLayout);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = e->computePipelineLayout;

    vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &e->computePipeline);

    // Descriptor pools
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;

    vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &e->descriptorPool);

    VkDescriptorPoolCreateInfo blitPoolInfo{};
    blitPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    blitPoolInfo.poolSizeCount = 1;
    blitPoolInfo.pPoolSizes = &poolSizes[2];
    blitPoolInfo.maxSets = 1;

    vkCreateDescriptorPool(e->device, &blitPoolInfo, nullptr, &e->blitDescriptorPool);

    // Allocate descriptor sets
    VkDescriptorSetAllocateInfo descAllocInfo{};
    descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAllocInfo.descriptorPool = e->descriptorPool;
    descAllocInfo.descriptorSetCount = 1;
    descAllocInfo.pSetLayouts = &e->descriptorSetLayout;

    vkAllocateDescriptorSets(e->device, &descAllocInfo, &e->descriptorSet);

    VkDescriptorImageInfo descImageInfo = {nullptr, e->computeImageView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo descBufferInfo = {e->uniformBuffer, 0, sizeof(UBO)};

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = e->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &descImageInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = e->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &descBufferInfo;

    vkUpdateDescriptorSets(e->device, 2, writes.data(), 0, nullptr);

    // Blit descriptor set
    VkDescriptorSetAllocateInfo blitAllocInfo{};
    blitAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    blitAllocInfo.descriptorPool = e->blitDescriptorPool;
    blitAllocInfo.descriptorSetCount = 1;
    blitAllocInfo.pSetLayouts = &e->blitDescriptorSetLayout;

    vkAllocateDescriptorSets(e->device, &blitAllocInfo, &e->blitDescriptorSet);

    VkDescriptorImageInfo blitImageInfo = {e->sampler, e->computeImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet blitWrite{};
    blitWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    blitWrite.dstSet = e->blitDescriptorSet;
    blitWrite.dstBinding = 0;
    blitWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blitWrite.descriptorCount = 1;
    blitWrite.pImageInfo = &blitImageInfo;

    vkUpdateDescriptorSets(e->device, 1, &blitWrite, 0, nullptr);

    // Create depth buffer
    {
        VkImageCreateInfo depthImageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
        depthImageInfo.format = e->depthFormat;
        depthImageInfo.extent = {g_framebufferWidth, g_framebufferHeight, 1};
        depthImageInfo.mipLevels = 1;
        depthImageInfo.arrayLayers = 1;
        depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        vkCreateImage(e->device, &depthImageInfo, nullptr, &e->depthImage);

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(e->device, e->depthImage, &memReq);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(e->device, &allocInfo, nullptr, &e->depthMemory);
        vkBindImageMemory(e->device, e->depthImage, e->depthMemory, 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = e->depthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = e->depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(e->device, &viewInfo, nullptr, &e->depthImageView);
    }

    // Render pass with depth attachment
    VkAttachmentDescription attachments[2] = {};

    // Color attachment
    attachments[0].format = e->swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    attachments[1].format = e->depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    vkCreateRenderPass(e->device, &renderPassInfo, nullptr, &e->renderPass);

    // Framebuffers with depth attachment
    e->framebuffers.resize(e->swapchainImageViews.size());
    for (size_t i = 0; i < e->swapchainImageViews.size(); i++) {
        VkImageView fbAttachments[2] = {e->swapchainImageViews[i], e->depthImageView};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = e->renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments;
        fbInfo.width = g_framebufferWidth;
        fbInfo.height = g_framebufferHeight;
        fbInfo.layers = 1;

        vkCreateFramebuffer(e->device, &fbInfo, nullptr, &e->framebuffers[i]);
    }

    // Blit pipeline
    auto vertCode = read_file(e->shaderDir + "/blit.vert.spv");
    auto fragCode = read_file(e->shaderDir + "/blit.frag.spv");

    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "Failed to load blit shaders" << std::endl;
        return false;
    }

    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo vertInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vertInfo.codeSize = vertCode.size();
    vertInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    vkCreateShaderModule(e->device, &vertInfo, nullptr, &vertModule);

    VkShaderModuleCreateInfo fragInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fragInfo.codeSize = fragCode.size();
    fragInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    vkCreateShaderModule(e->device, &fragInfo, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, 0, (float)g_framebufferWidth, (float)g_framebufferHeight, 0, 1};
    VkRect2D scissor = {{0, 0}, {g_framebufferWidth, g_framebufferHeight}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo blitPipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    blitPipelineLayoutInfo.setLayoutCount = 1;
    blitPipelineLayoutInfo.pSetLayouts = &e->blitDescriptorSetLayout;

    vkCreatePipelineLayout(e->device, &blitPipelineLayoutInfo, nullptr, &e->graphicsPipelineLayout);

    VkGraphicsPipelineCreateInfo graphicsPipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    graphicsPipelineInfo.stageCount = 2;
    graphicsPipelineInfo.pStages = shaderStages;
    graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
    graphicsPipelineInfo.pInputAssemblyState = &inputAssembly;
    graphicsPipelineInfo.pViewportState = &viewportState;
    graphicsPipelineInfo.pRasterizationState = &rasterizer;
    graphicsPipelineInfo.pMultisampleState = &multisampling;
    graphicsPipelineInfo.pColorBlendState = &colorBlending;
    graphicsPipelineInfo.layout = e->graphicsPipelineLayout;
    graphicsPipelineInfo.renderPass = e->renderPass;

    vkCreateGraphicsPipelines(e->device, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, nullptr, &e->graphicsPipeline);

    vkDestroyShaderModule(e->device, vertModule, nullptr);
    vkDestroyShaderModule(e->device, fragModule, nullptr);

    // Command pool and buffers
    VkCommandPoolCreateInfo cmdPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmdPoolInfo.queueFamilyIndex = e->graphicsFamily;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    vkCreateCommandPool(e->device, &cmdPoolInfo, nullptr, &e->commandPool);

    e->commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = e->commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    vkAllocateCommandBuffers(e->device, &cmdAllocInfo, e->commandBuffers.data());

    // Sync objects
    e->imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    e->renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    e->inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(e->device, &semaphoreInfo, nullptr, &e->imageAvailableSemaphores[i]);
        vkCreateSemaphore(e->device, &semaphoreInfo, nullptr, &e->renderFinishedSemaphores[i]);
        vkCreateFence(e->device, &fenceInfo, nullptr, &e->inFlightFences[i]);
    }

    // Initialize default scene objects
    e->initDefaultScene();

    // Initialize ImGui
    {
        // Create descriptor pool for ImGui
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 100;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &e->imguiDescriptorPool);

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        // Initialize ImGui for SDL3
        ImGui_ImplSDL3_InitForVulkan(e->window);

        // Initialize ImGui for Vulkan
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = e->instance;
        initInfo.PhysicalDevice = e->physicalDevice;
        initInfo.Device = e->device;
        initInfo.QueueFamily = e->graphicsFamily;
        initInfo.Queue = e->graphicsQueue;
        initInfo.DescriptorPool = e->imguiDescriptorPool;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = (uint32_t)e->swapchainImages.size();
        initInfo.PipelineInfoMain.RenderPass = e->renderPass;
        ImGui_ImplVulkan_Init(&initInfo);

        std::cout << "ImGui initialized!" << std::endl;
        // Note: SDL3 events are processed in poll_events(), no callbacks needed
    }

    // Initialize shader switching (currentShaderName already set above before shader loading)
    scan_shaders();  // Scan for available .comp files

    e->initialized = true;
    std::cout << "Vulkan initialized!" << std::endl;
    std::cout << "UBO size: " << sizeof(UBO) << " bytes" << std::endl;
    std::cout << "Scene has " << e->objects.size() << " objects" << std::endl;
    return true;
}

inline bool should_close() {
    auto* e = get_engine();
    return !e || !e->window || !e->running || g_sigint_received;
}

// =============================================================================
// Event Buffer System - allows jank to handle event dispatch
// =============================================================================

constexpr int MAX_EVENTS_PER_FRAME = 64;

// Event types matching SDL3
constexpr uint32_t EVENT_QUIT = SDL_EVENT_QUIT;
constexpr uint32_t EVENT_KEY_DOWN = SDL_EVENT_KEY_DOWN;
constexpr uint32_t EVENT_KEY_UP = SDL_EVENT_KEY_UP;
constexpr uint32_t EVENT_MOUSE_BUTTON_DOWN = SDL_EVENT_MOUSE_BUTTON_DOWN;
constexpr uint32_t EVENT_MOUSE_BUTTON_UP = SDL_EVENT_MOUSE_BUTTON_UP;
constexpr uint32_t EVENT_MOUSE_MOTION = SDL_EVENT_MOUSE_MOTION;
constexpr uint32_t EVENT_MOUSE_WHEEL = SDL_EVENT_MOUSE_WHEEL;

struct EventData {
    uint32_t type;
    // Key event data
    int32_t key_code;
    uint16_t key_mod;
    // Mouse button data
    uint8_t mouse_button;
    float mouse_x;
    float mouse_y;
    // Mouse motion data
    float mouse_xrel;
    float mouse_yrel;
    // Scroll data
    float scroll_x;
    float scroll_y;
    // ImGui capture flags (set at event poll time)
    bool imgui_wants_keyboard;
    bool imgui_wants_mouse;
};

// Global event buffer
inline EventData g_event_buffer[MAX_EVENTS_PER_FRAME];
inline int g_event_count = 0;

// Poll all SDL events into buffer, let ImGui process them, but don't dispatch to handlers
inline int poll_events_only() {
    auto* e = get_engine();
    if (!e || !e->initialized) return 0;

    g_event_count = 0;
    SDL_Event event;

    while (SDL_PollEvent(&event) && g_event_count < MAX_EVENTS_PER_FRAME) {
        // Let ImGui process the event first
        ImGui_ImplSDL3_ProcessEvent(&event);

        // Store event data for jank to process
        EventData& ed = g_event_buffer[g_event_count];
        ed.type = event.type;
        ed.key_code = 0;
        ed.key_mod = 0;
        ed.mouse_button = 0;
        ed.mouse_x = 0;
        ed.mouse_y = 0;
        ed.mouse_xrel = 0;
        ed.mouse_yrel = 0;
        ed.scroll_x = 0;
        ed.scroll_y = 0;
        ed.imgui_wants_keyboard = ImGui::GetIO().WantCaptureKeyboard;
        ed.imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;

        switch (event.type) {
            case SDL_EVENT_QUIT:
                e->running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                ed.key_code = event.key.key;
                ed.key_mod = event.key.mod;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                ed.mouse_button = event.button.button;
                ed.mouse_x = event.button.x;
                ed.mouse_y = event.button.y;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                ed.mouse_x = event.motion.x;
                ed.mouse_y = event.motion.y;
                ed.mouse_xrel = event.motion.xrel;
                ed.mouse_yrel = event.motion.yrel;
                // Update lastMouse for internal tracking
                e->lastMouseX = event.motion.x;
                e->lastMouseY = event.motion.y;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                ed.scroll_x = event.wheel.x;
                ed.scroll_y = event.wheel.y;
                break;
        }

        g_event_count++;
    }

    return g_event_count;
}

// Event buffer accessors for jank
inline int get_event_count() { return g_event_count; }
inline uint32_t get_event_type(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].type : 0; }
inline int32_t get_event_key_code(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].key_code : 0; }
inline uint16_t get_event_key_mod(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].key_mod : 0; }
inline uint8_t get_event_mouse_button(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].mouse_button : 0; }
inline float get_event_mouse_x(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].mouse_x : 0; }
inline float get_event_mouse_y(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].mouse_y : 0; }
inline float get_event_mouse_xrel(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].mouse_xrel : 0; }
inline float get_event_mouse_yrel(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].mouse_yrel : 0; }
inline float get_event_scroll_x(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].scroll_x : 0; }
inline float get_event_scroll_y(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].scroll_y : 0; }
inline bool get_event_imgui_wants_keyboard(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].imgui_wants_keyboard : false; }
inline bool get_event_imgui_wants_mouse(int idx) { return (idx >= 0 && idx < g_event_count) ? g_event_buffer[idx].imgui_wants_mouse : false; }

// Key code constants for jank
inline int32_t key_code_escape() { return SDLK_ESCAPE; }
inline int32_t key_code_r() { return SDLK_R; }
inline int32_t key_code_e() { return SDLK_E; }
inline int32_t key_code_z() { return SDLK_Z; }
inline int32_t key_code_d() { return SDLK_D; }
inline int32_t key_code_backspace() { return SDLK_BACKSPACE; }
inline int32_t key_code_delete() { return SDLK_DELETE; }
inline int32_t key_code_0() { return SDLK_0; }
inline int32_t key_code_1() { return SDLK_1; }
inline int32_t key_code_9() { return SDLK_9; }
inline int32_t key_code_page_up() { return SDLK_PAGEUP; }
inline int32_t key_code_page_down() { return SDLK_PAGEDOWN; }
inline int32_t key_code_left() { return SDLK_LEFT; }
inline int32_t key_code_right() { return SDLK_RIGHT; }
inline uint16_t key_mod_gui() { return SDL_KMOD_GUI; }
inline uint16_t key_mod_shift() { return SDL_KMOD_SHIFT; }

// Mouse button constants
inline uint8_t mouse_button_left() { return SDL_BUTTON_LEFT; }
inline uint8_t mouse_button_right() { return SDL_BUTTON_RIGHT; }
inline uint8_t mouse_button_middle() { return SDL_BUTTON_MIDDLE; }

inline void update_uniforms(double dt) {
    auto* e = get_engine();
    if (!e) return;

    // Note: shader reload check moved to jank (render/check-shader-reload!)

    e->time += static_cast<float>(dt);

    UBO ubo{};
    e->camera.getPosition(ubo.cameraPos);
    ubo.cameraPos[3] = 1.5f;

    e->camera.getTarget(ubo.cameraTarget);

    float lightLen = sqrt(1 + 4 + 1);
    ubo.lightDir[0] = 1 / lightLen;
    ubo.lightDir[1] = 2 / lightLen;
    ubo.lightDir[2] = 1 / lightLen;

    ubo.resolution[0] = g_framebufferWidth;
    ubo.resolution[1] = g_framebufferHeight;
    ubo.resolution[2] = e->time;
    ubo.resolution[3] = e->meshScale;  // Mesh scale in w component

    // Mesh preview options
    ubo.options[0] = e->meshUseVertexColors ? 1.0f : 0.0f;
    ubo.options[1] = 0.0f;
    ubo.options[2] = 0.0f;
    ubo.options[3] = 0.0f;

    // Edit mode uniforms
    ubo.editMode[0] = e->editMode ? 1.0f : 0.0f;
    ubo.editMode[1] = static_cast<float>(e->selectedObject);
    ubo.editMode[2] = static_cast<float>(e->hoveredAxis);
    ubo.editMode[3] = static_cast<float>(e->objects.size());  // Object count

    ubo.gizmoPos[0] = e->selectedPos[0];
    ubo.gizmoPos[1] = e->selectedPos[1];
    ubo.gizmoPos[2] = e->selectedPos[2];
    ubo.gizmoPos[3] = 0.0f;

    ubo.gizmoRot[0] = e->selectedRot[0];
    ubo.gizmoRot[1] = e->selectedRot[1];
    ubo.gizmoRot[2] = e->selectedRot[2];
    ubo.gizmoRot[3] = 0.0f;

    // Object positions and rotations from Engine's objects vector (extensible)
    size_t numObjects = std::min(e->objects.size(), (size_t)MAX_OBJECTS);
    for (size_t i = 0; i < numObjects; i++) {
        ubo.objPositions[i][0] = e->objects[i].position[0];
        ubo.objPositions[i][1] = e->objects[i].position[1];
        ubo.objPositions[i][2] = e->objects[i].position[2];
        ubo.objPositions[i][3] = static_cast<float>(e->objects[i].type);

        ubo.objRotations[i][0] = e->objects[i].rotation[0];
        ubo.objRotations[i][1] = e->objects[i].rotation[1];
        ubo.objRotations[i][2] = e->objects[i].rotation[2];
        ubo.objRotations[i][3] = 0.0f;
    }
    // Zero out unused slots
    for (size_t i = numObjects; i < MAX_OBJECTS; i++) {
        ubo.objPositions[i][0] = 0.0f;
        ubo.objPositions[i][1] = 0.0f;
        ubo.objPositions[i][2] = 0.0f;
        ubo.objPositions[i][3] = 0.0f;
        ubo.objRotations[i][0] = 0.0f;
        ubo.objRotations[i][1] = 0.0f;
        ubo.objRotations[i][2] = 0.0f;
        ubo.objRotations[i][3] = 0.0f;
    }

    memcpy(e->uniformMapped, &ubo, sizeof(UBO));
}

// Forward declarations for mesh preview
inline void render_mesh_preview(VkCommandBuffer cmd);
inline void cleanup_mesh_preview();

inline void draw_frame() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    vkWaitForFences(e->device, 1, &e->inFlightFences[e->currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(e->device, e->swapchain, UINT64_MAX,
                          e->imageAvailableSemaphores[e->currentFrame], VK_NULL_HANDLE, &imageIndex);
    e->currentImageIndex = imageIndex;  // Store for viewport screenshot

    vkResetFences(e->device, 1, &e->inFlightFences[e->currentFrame]);

    // ImGui frame start (called from Jank via imgui_new_frame)
    // ImGui render happens later via imgui_render

    VkCommandBuffer cmd = e->commandBuffers[e->currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Only run compute shader when dirty (scene changed)
    if (e->dirty) {
        // Transition to general
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = e->computeImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Dispatch compute
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->computePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->computePipelineLayout,
                               0, 1, &e->descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, (g_framebufferWidth + 15) / 16, (g_framebufferHeight + 15) / 16, 1);

        // Transition to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // In continuous mode, keep dirty true for constant re-rendering
        if (!e->continuousMode) {
            e->dirty = false;
        }
    }
    // When clean, computeImage is already in SHADER_READ_ONLY_OPTIMAL from previous frame

    // Render pass with depth clear
    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{0, 0, 0, 1}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = e->renderPass;
    renderPassInfo.framebuffer = e->framebuffers[imageIndex];
    renderPassInfo.renderArea = {{0, 0}, {g_framebufferWidth, g_framebufferHeight}};
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // If mesh preview is visible and solid mode, skip SDF and render only mesh
    bool meshOnly = e->meshPreviewVisible && e->meshRenderSolid && e->meshPipelineInitialized && e->meshIndexCount > 0;

    if (!meshOnly) {
        // Render SDF raymarching result
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->graphicsPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->graphicsPipelineLayout,
                               0, 1, &e->blitDescriptorSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Render mesh preview if visible
    render_mesh_preview(cmd);

    // Render ImGui draw data (if imgui_render was called before draw_frame)
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &e->imageAvailableSemaphores[e->currentFrame];
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &e->renderFinishedSemaphores[e->currentFrame];

    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, e->inFlightFences[e->currentFrame]);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &e->renderFinishedSemaphores[e->currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(e->presentQueue, &presentInfo);

    e->currentFrame = (e->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

inline void cleanup() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    vkDeviceWaitIdle(e->device);

    // Cleanup ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(e->device, e->imguiDescriptorPool, nullptr);

    // Cleanup mesh preview
    cleanup_mesh_preview();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(e->device, e->inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(e->device, e->commandPool, nullptr);
    for (auto fb : e->framebuffers) vkDestroyFramebuffer(e->device, fb, nullptr);

    vkDestroyPipeline(e->device, e->graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(e->device, e->graphicsPipelineLayout, nullptr);
    vkDestroyRenderPass(e->device, e->renderPass, nullptr);

    vkDestroyPipeline(e->device, e->computePipeline, nullptr);
    vkDestroyPipelineLayout(e->device, e->computePipelineLayout, nullptr);
    vkDestroyShaderModule(e->device, e->computeShaderModule, nullptr);

    vkDestroyDescriptorPool(e->device, e->descriptorPool, nullptr);
    vkDestroyDescriptorPool(e->device, e->blitDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(e->device, e->descriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(e->device, e->blitDescriptorSetLayout, nullptr);

    vkDestroySampler(e->device, e->sampler, nullptr);
    vkDestroyImageView(e->device, e->computeImageView, nullptr);
    vkDestroyImage(e->device, e->computeImage, nullptr);
    vkFreeMemory(e->device, e->computeImageMemory, nullptr);

    vkDestroyBuffer(e->device, e->uniformBuffer, nullptr);
    vkFreeMemory(e->device, e->uniformMemory, nullptr);

    for (auto iv : e->swapchainImageViews) vkDestroyImageView(e->device, iv, nullptr);

    vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    vkDestroyDevice(e->device, nullptr);
    vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
    vkDestroyInstance(e->instance, nullptr);

    SDL_DestroyWindow(e->window);
    SDL_Quit();

    e->initialized = false;
    std::cout << "Cleaned up" << std::endl;
}

// Compile GLSL to SPIR-V and store internally (shaderc is C++, must stay here)
// Returns: 0 = success, 1 = compile error
inline int compile_glsl_to_spirv_stored(const char* glsl_source, const char* shader_name) {
    auto* e = get_engine();
    if (!e || !e->initialized) return 1;

    auto spirv = compile_glsl_to_spirv(glsl_source, shader_name, shaderc_compute_shader);
    if (spirv.empty()) {
        return 1;  // Compile error
    }

    e->pendingSpirvData = std::move(spirv);
    return 0;  // Success
}

// SPIR-V data accessors for jank
inline const uint32_t* get_pending_spirv_data() {
    auto* e = get_engine();
    return (e && !e->pendingSpirvData.empty()) ? e->pendingSpirvData.data() : nullptr;
}

inline size_t get_pending_spirv_size_bytes() {
    auto* e = get_engine();
    return e ? e->pendingSpirvData.size() * sizeof(uint32_t) : 0;
}

inline void clear_pending_spirv() {
    auto* e = get_engine();
    if (e) e->pendingSpirvData.clear();
}

// Vulkan handle accessors for jank pipeline recreation
inline VkDescriptorSetLayout get_descriptor_set_layout() {
    auto* e = get_engine();
    return e ? e->descriptorSetLayout : VK_NULL_HANDLE;
}

inline const VkDescriptorSetLayout* get_descriptor_set_layout_ptr() {
    auto* e = get_engine();
    return e ? &e->descriptorSetLayout : nullptr;
}

inline VkShaderModule get_compute_shader_module() {
    auto* e = get_engine();
    return e ? e->computeShaderModule : VK_NULL_HANDLE;
}

inline void set_compute_shader_module(VkShaderModule m) {
    auto* e = get_engine();
    if (e) e->computeShaderModule = m;
}

inline void set_compute_pipeline(VkPipeline p) {
    auto* e = get_engine();
    if (e) e->computePipeline = p;
}

inline void set_compute_pipeline_layout(VkPipelineLayout l) {
    auto* e = get_engine();
    if (e) e->computePipelineLayout = l;
}

inline double get_time() {
    auto* e = get_engine();
    return e ? e->time : 0;
}

// Edit mode API for Jank
inline bool get_edit_mode() {
    auto* e = get_engine();
    return e ? e->editMode : false;
}

inline int get_selected_object() {
    auto* e = get_engine();
    return e ? e->selectedObject : -1;
}

inline int get_hovered_axis() {
    auto* e = get_engine();
    return e ? e->hoveredAxis : -1;
}

inline int get_dragging_axis() {
    auto* e = get_engine();
    return e ? e->draggingAxis : -1;
}

inline void set_continuous_mode(bool enabled) {
    auto* e = get_engine();
    if (e) {
        e->continuousMode = enabled;
        if (enabled) e->dirty = true;
    }
}

inline void set_dirty() {
    auto* e = get_engine();
    if (e) e->dirty = true;
}

// Edit mode helpers for jank event handling
inline void toggle_edit_mode() {
    auto* e = get_engine();
    if (!e) return;
    e->editMode = !e->editMode;
    e->dirty = true;
    if (e->editMode) {
        std::cout << "Edit mode enabled" << std::endl;
    } else {
        std::cout << "Edit mode disabled" << std::endl;
    }
}

inline void select_object_by_id(int id) {
    auto* e = get_engine();
    if (!e) return;
    select_object(e, id);
}

inline void request_undo() {
    auto* e = get_engine();
    if (e) e->undoRequested = true;
}

inline void request_redo() {
    auto* e = get_engine();
    if (e) e->redoRequested = true;
}

inline void request_duplicate() {
    auto* e = get_engine();
    if (e) e->duplicateRequested = true;
}

inline void request_delete() {
    auto* e = get_engine();
    if (e) e->deleteRequested = true;
}

inline void request_reset_transform() {
    auto* e = get_engine();
    if (e) e->resetTransformRequested = true;
}

inline float get_camera_distance() {
    auto* e = get_engine();
    return e ? e->camera.distance : 10.0f;
}

inline void set_camera_distance(float d) {
    auto* e = get_engine();
    if (e) e->camera.distance = d;
}

inline float get_camera_angle_x() {
    auto* e = get_engine();
    return e ? e->camera.angleX : 0.0f;
}

inline void set_camera_angle_x(float a) {
    auto* e = get_engine();
    if (e) e->camera.angleX = a;
}

inline float get_camera_angle_y() {
    auto* e = get_engine();
    return e ? e->camera.angleY : 0.5f;
}

inline void set_camera_angle_y(float a) {
    auto* e = get_engine();
    if (e) e->camera.angleY = a;
}

inline float get_camera_target_y() {
    auto* e = get_engine();
    return e ? e->camera.targetY : 0.0f;
}

inline void set_camera_target_y(float y) {
    auto* e = get_engine();
    if (e) e->camera.targetY = y;
}

inline const char* get_current_shader_name() {
    auto* e = get_engine();
    return e ? e->currentShaderName.c_str() : "";
}

// Shader list management - allows jank to manage shader switching
inline int get_shader_count() {
    auto* e = get_engine();
    return e ? static_cast<int>(e->shaderList.size()) : 0;
}

inline const char* get_shader_name_at(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < static_cast<int>(e->shaderList.size())) {
        return e->shaderList[idx].c_str();
    }
    return "";
}

inline int get_current_shader_index() {
    auto* e = get_engine();
    return e ? e->currentShaderIndex : 0;
}

inline void load_shader_at_index(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < static_cast<int>(e->shaderList.size())) {
        e->currentShaderIndex = idx;
        load_shader_by_name(e->shaderList[idx]);
    }
}

// Shader reload helpers - allows jank to manage auto-reload
inline const char* get_shader_dir() {
    auto* e = get_engine();
    return e ? e->shaderDir.c_str() : "";
}

inline int64_t get_file_mod_time(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return static_cast<int64_t>(st.st_mtime);
    }
    return 0;
}

inline int64_t get_last_shader_mod_time() {
    auto* e = get_engine();
    return e ? static_cast<int64_t>(e->lastShaderModTime) : 0;
}

inline void set_last_shader_mod_time(int64_t t) {
    auto* e = get_engine();
    if (e) e->lastShaderModTime = static_cast<time_t>(t);
}

// Pending shader switch - consumed by Jank main loop
inline int get_pending_shader_switch() {
    auto* e = get_engine();
    return e ? e->pendingShaderSwitch : 0;
}

inline void clear_pending_shader_switch() {
    auto* e = get_engine();
    if (e) e->pendingShaderSwitch = 0;
}

inline void set_pending_shader_switch(int direction) {
    auto* e = get_engine();
    if (e) e->pendingShaderSwitch = direction;
}

// Individual component accessors for jank
inline float get_object_pos_x(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].position[0];
    return 0.0f;
}
inline float get_object_pos_y(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].position[1];
    return 0.0f;
}
inline float get_object_pos_z(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].position[2];
    return 0.0f;
}
inline float get_object_rot_x(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].rotation[0];
    return 0.0f;
}
inline float get_object_rot_y(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].rotation[1];
    return 0.0f;
}
inline float get_object_rot_z(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].rotation[2];
    return 0.0f;
}
inline int get_object_type_id(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].type;
    return 0;
}
inline bool get_object_selectable(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) return e->objects[idx].selectable;
    return false;
}

inline int get_object_count() {
    auto* e = get_engine();
    return e ? (int)e->objects.size() : 0;
}

// ============================================================================
// Engine Field Accessors for jank interop
// ============================================================================

inline bool engine_initialized() {
    auto* e = get_engine();
    return e && e->device && e->computeImage;
}

inline VkDevice get_device() {
    auto* e = get_engine();
    return e ? e->device : VK_NULL_HANDLE;
}

inline VkCommandPool get_command_pool() {
    auto* e = get_engine();
    return e ? e->commandPool : VK_NULL_HANDLE;
}

inline VkQueue get_graphics_queue() {
    auto* e = get_engine();
    return e ? e->graphicsQueue : VK_NULL_HANDLE;
}

inline VkImage get_compute_image() {
    auto* e = get_engine();
    return e ? e->computeImage : VK_NULL_HANDLE;
}

inline VkPipeline get_compute_pipeline() {
    auto* e = get_engine();
    return e ? e->computePipeline : VK_NULL_HANDLE;
}

inline VkPipelineLayout get_compute_pipeline_layout() {
    auto* e = get_engine();
    return e ? e->computePipelineLayout : VK_NULL_HANDLE;
}

inline VkDescriptorSet get_descriptor_set() {
    auto* e = get_engine();
    return e ? e->descriptorSet : VK_NULL_HANDLE;
}

inline uint32_t get_swapchain_width() {
    auto* e = get_engine();
    return e ? e->swapchainExtent.width : 0;
}

inline uint32_t get_swapchain_height() {
    auto* e = get_engine();
    return e ? e->swapchainExtent.height : 0;
}

// Allocate a single command buffer - needed because jank can't handle VkCommandBuffer output params
inline VkCommandBuffer alloc_screenshot_cmd() {
    auto* e = get_engine();
    if (!e) return VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = e->commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(e->device, &info, &cmd);
    return cmd;
}

// Struct for returning screenshot buffer handles
struct ScreenshotBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    bool success;
};

// Create staging buffer, returns buffer and memory in struct
inline ScreenshotBuffer create_screenshot_buffer(VkDeviceSize size) {
    ScreenshotBuffer result{VK_NULL_HANDLE, VK_NULL_HANDLE, false};
    auto* e = get_engine();
    if (!e) return result;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(e->device, &info, nullptr, &result.buffer) != VK_SUCCESS) return result;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, result.buffer, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &result.memory) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, result.buffer, nullptr);
        result.buffer = VK_NULL_HANDLE;
        return result;
    }
    vkBindBufferMemory(e->device, result.buffer, result.memory, 0);
    result.success = true;
    return result;
}

// Save viewport screenshot (captures final rendered frame with mesh overlay)
inline bool save_viewport_screenshot(const char* filepath) {
    auto* e = get_engine();
    if (!e || !e->initialized) return false;

    uint32_t width = e->swapchainExtent.width;
    uint32_t height = e->swapchainExtent.height;
    VkDeviceSize imageSize = width * height * 4;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = imageSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(e->device, &bufInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, stagingBuffer, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, stagingBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(e->device, stagingBuffer, stagingMemory, 0);

    // Get current swapchain image
    VkImage srcImage = e->swapchainImages[e->currentImageIndex];

    // Allocate command buffer
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = e->commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(e->device, &cmdInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition swapchain image to transfer src
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = srcImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition back to present
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(e->graphicsQueue);

    // Map memory and save
    void* data;
    vkMapMemory(e->device, stagingMemory, 0, imageSize, 0, &data);

    // Convert BGRA to RGB and flip vertically
    uint8_t* pixels = static_cast<uint8_t*>(data);
    uint8_t* rgb = new uint8_t[width * height * 3];
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t srcIdx = (y * width + x) * 4;
            uint32_t dstIdx = ((height - 1 - y) * width + x) * 3;
            rgb[dstIdx] = pixels[srcIdx + 2];     // R (from B)
            rgb[dstIdx + 1] = pixels[srcIdx + 1]; // G
            rgb[dstIdx + 2] = pixels[srcIdx];     // B (from R)
        }
    }

    int result = stbi_write_png(filepath, width, height, 3, rgb, width * 3);
    delete[] rgb;

    vkUnmapMemory(e->device, stagingMemory);
    vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmd);
    vkDestroyBuffer(e->device, stagingBuffer, nullptr);
    vkFreeMemory(e->device, stagingMemory, nullptr);

    std::cout << "Viewport screenshot saved to " << filepath << " (" << width << "x" << height << ")" << std::endl;
    return result != 0;
}

// Write PNG with downsampling - single helper function for jank
// jank's GC can't handle ~230k loop iterations, so this is in C++
inline int write_png_downsampled(const char* filepath, const void* pixelsVoid,
                                  uint32_t width, uint32_t height, uint32_t scale) {
    const uint8_t* pixels = static_cast<const uint8_t*>(pixelsVoid);
    uint32_t out_w = width / scale;
    uint32_t out_h = height / scale;
    size_t rgb_size = out_w * out_h * 3;
    uint8_t* rgb = new uint8_t[rgb_size];

    // Downsample with vertical flip
    for (uint32_t y = 0; y < out_h; y++) {
        for (uint32_t x = 0; x < out_w; x++) {
            uint32_t src_y = (out_h - 1 - y) * scale + scale / 2;
            uint32_t src_x = x * scale + scale / 2;
            size_t src_idx = (src_y * width + src_x) * 4;
            size_t dst_idx = (y * out_w + x) * 3;
            rgb[dst_idx] = pixels[src_idx];
            rgb[dst_idx + 1] = pixels[src_idx + 1];
            rgb[dst_idx + 2] = pixels[src_idx + 2];
        }
    }

    int result = stbi_write_png(filepath, out_w, out_h, 3, rgb, out_w * 3);
    delete[] rgb;
    return result;
}

// ============================================================================
// Mesh Export Result (used by both libfive and GPU-based export)
// ============================================================================

struct MeshExportResult {
    bool success;
    size_t vertices;
    size_t triangles;
    const char* message;
};

// ============================================================================
// GPU-Based SDF Sampling and Mesh Export
// ============================================================================
// This system samples the actual GPU-rendered SDF and converts it to a mesh,
// avoiding code duplication between shader and C++ SDF definitions.

struct SDFSampler {
    bool initialized = false;
    // No more inputBuffer - positions computed on-the-fly in shader!
    VkBuffer outputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory = VK_NULL_HANDLE;
    VkBuffer paramsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory paramsMemory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    size_t maxPoints = 0;
    std::string cachedShaderName;
    time_t cachedShaderModTime = 0;  // Track shader modification for hot reload
};

inline SDFSampler* g_sampler = nullptr;

inline SDFSampler* get_sampler() {
    if (!g_sampler) {
        g_sampler = new SDFSampler();
    }
    return g_sampler;
}

// Extract sceneSDF function and its dependencies from a shader source
inline std::string extract_scene_sdf(const std::string& shaderSource) {
    std::string result;

    size_t sceneSdfPos = shaderSource.find("float sceneSDF(vec3 p)");
    if (sceneSdfPos == std::string::npos) {
        sceneSdfPos = shaderSource.find("float sceneSDF(");
    }
    if (sceneSdfPos == std::string::npos) {
        std::cerr << "Could not find sceneSDF function in shader" << std::endl;
        return "";
    }

    // Find the end of sceneSDF function
    size_t braceCount = 0;
    size_t funcStart = shaderSource.find('{', sceneSdfPos);
    if (funcStart == std::string::npos) return "";

    size_t funcEnd = funcStart;
    for (size_t i = funcStart; i < shaderSource.size(); i++) {
        if (shaderSource[i] == '{') braceCount++;
        else if (shaderSource[i] == '}') {
            braceCount--;
            if (braceCount == 0) {
                funcEnd = i + 1;
                break;
            }
        }
    }

    std::string sceneSdf = shaderSource.substr(sceneSdfPos, funcEnd - sceneSdfPos);

    // Find helper functions (primitives and operations)
    size_t helperStart = shaderSource.find("// SDF PRIMITIVES");
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("// BOOLEAN OPERATIONS");
    }
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("float opUnion");
    }
    if (helperStart == std::string::npos) {
        helperStart = 0;
    }

    std::string helpers = shaderSource.substr(helperStart, sceneSdfPos - helperStart);
    return helpers + "\n" + sceneSdf;
}

// Build the sampler shader from template and current scene
inline std::string build_sampler_shader(const std::string& sceneShaderPath) {
    std::string templatePath = sceneShaderPath.substr(0, sceneShaderPath.rfind('/')) + "/sdf_sampler.comp";
    std::string templateSrc = read_text_file(templatePath);
    if (templateSrc.empty()) {
        std::cerr << "Could not read sampler template: " << templatePath << std::endl;
        return "";
    }

    std::string sceneSrc = read_text_file(sceneShaderPath);
    if (sceneSrc.empty()) {
        std::cerr << "Could not read scene shader: " << sceneShaderPath << std::endl;
        return "";
    }

    std::string sceneCode = extract_scene_sdf(sceneSrc);
    if (sceneCode.empty()) {
        std::cerr << "Could not extract sceneSDF from shader" << std::endl;
        return "";
    }

    size_t markerStart = templateSrc.find("// MARKER_SCENE_SDF_START");
    size_t markerEnd = templateSrc.find("// MARKER_SCENE_SDF_END");
    if (markerStart == std::string::npos || markerEnd == std::string::npos) {
        std::cerr << "Could not find markers in sampler template" << std::endl;
        return "";
    }

    markerEnd = templateSrc.find('\n', markerEnd) + 1;
    return templateSrc.substr(0, markerStart) + "\n" + sceneCode + "\n" + templateSrc.substr(markerEnd);
}

inline void cleanup_sampler() {
    auto* s = get_sampler();
    auto* e = get_engine();
    if (!s->initialized || !e || !e->device) return;

    vkDeviceWaitIdle(e->device);

    if (s->pipeline) vkDestroyPipeline(e->device, s->pipeline, nullptr);
    if (s->pipelineLayout) vkDestroyPipelineLayout(e->device, s->pipelineLayout, nullptr);
    if (s->shaderModule) vkDestroyShaderModule(e->device, s->shaderModule, nullptr);
    if (s->descriptorPool) vkDestroyDescriptorPool(e->device, s->descriptorPool, nullptr);
    if (s->descriptorSetLayout) vkDestroyDescriptorSetLayout(e->device, s->descriptorSetLayout, nullptr);

    // No more inputBuffer - positions computed on-the-fly in shader
    if (s->outputBuffer) vkDestroyBuffer(e->device, s->outputBuffer, nullptr);
    if (s->outputMemory) vkFreeMemory(e->device, s->outputMemory, nullptr);
    if (s->paramsBuffer) vkDestroyBuffer(e->device, s->paramsBuffer, nullptr);
    if (s->paramsMemory) vkFreeMemory(e->device, s->paramsMemory, nullptr);

    s->initialized = false;
    s->maxPoints = 0;
    s->cachedShaderName = "";
}

inline bool init_sampler(size_t numPoints) {
    auto* s = get_sampler();
    auto* e = get_engine();
    if (!e || !e->initialized) {
        std::cerr << "Engine not initialized" << std::endl;
        return false;
    }

    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    time_t currentModTime = 0;
    struct stat st;
    if (stat(shaderPath.c_str(), &st) == 0) {
        currentModTime = st.st_mtime;
    }

    // Check if we can reuse cached sampler (same shader name, same mod time, enough capacity)
    if (s->initialized && s->maxPoints >= numPoints &&
        s->cachedShaderName == e->currentShaderName &&
        s->cachedShaderModTime == currentModTime) {
        return true;
    }

    if (s->initialized) {
        cleanup_sampler();
    }

    std::cout << "Initializing SDF sampler for " << numPoints << " points..." << std::endl;

    std::string samplerSrc = build_sampler_shader(shaderPath);
    if (samplerSrc.empty()) {
        return false;
    }

    auto spirv = compile_glsl_to_spirv(samplerSrc, "sdf_sampler.comp", shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "Failed to compile sampler shader" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderModuleInfo.pCode = spirv.data();

    if (vkCreateShaderModule(e->device, &shaderModuleInfo, nullptr, &s->shaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create sampler shader module" << std::endl;
        return false;
    }

    // MEMORY OPTIMIZED: No more input buffer - positions computed in shader!
    // Only need output buffer for distances and params buffer for grid parameters
    VkDeviceSize outputSize = numPoints * sizeof(float);
    // Params: resolution(uint) + time(float) + minXYZ(3 floats) + maxXYZ(3 floats) = 32 bytes
    VkDeviceSize paramsSize = 32;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Output buffer (distances)
    bufferInfo.size = outputSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &s->outputBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, s->outputBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &s->outputMemory) != VK_SUCCESS) {
        return false;
    }
    vkBindBufferMemory(e->device, s->outputBuffer, s->outputMemory, 0);

    // Params buffer (uniform)
    bufferInfo.size = paramsSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &s->paramsBuffer) != VK_SUCCESS) {
        return false;
    }

    vkGetBufferMemoryRequirements(e->device, s->paramsBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &s->paramsMemory) != VK_SUCCESS) {
        return false;
    }
    vkBindBufferMemory(e->device, s->paramsBuffer, s->paramsMemory, 0);

    // Descriptor layout: binding 0 = output (storage), binding 1 = params (uniform)
    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(e->device, &layoutInfo, nullptr, &s->descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &s->descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = s->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkAllocateDescriptorSets(e->device, &dsAllocInfo, &s->descriptorSet) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo outputBufferInfo{s->outputBuffer, 0, outputSize};
    VkDescriptorBufferInfo paramsBufferInfo{s->paramsBuffer, 0, paramsSize};

    VkWriteDescriptorSet writes[2] = {};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputBufferInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &paramsBufferInfo, nullptr};

    vkUpdateDescriptorSets(e->device, 2, writes, 0, nullptr);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &s->pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = s->shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = s->pipelineLayout;

    if (vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s->pipeline) != VK_SUCCESS) {
        return false;
    }

    s->maxPoints = numPoints;
    s->cachedShaderName = e->currentShaderName;
    s->cachedShaderModTime = currentModTime;  // Track mod time for hot reload
    s->initialized = true;

    std::cout << "SDF sampler initialized (memory optimized - no positions buffer!)" << std::endl;
    return true;
}

// Sample SDF on a regular 3D grid for marching cubes (memory optimized - positions computed in shader)
inline std::vector<float> sample_sdf_grid(
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    int res) {

    auto* s = get_sampler();
    auto* e = get_engine();

    size_t totalPoints = static_cast<size_t>(res) * res * res;
    if (totalPoints == 0) return {};

    std::cout << "Sampling " << totalPoints << " SDF points on GPU (memory optimized)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    if (!init_sampler(totalPoints)) {
        std::cerr << "Failed to initialize sampler" << std::endl;
        return {};
    }

    // Upload grid parameters to uniform buffer (no positions buffer needed!)
    // Struct matches shader: resolution(uint), time(float), minXYZ(3 floats), maxXYZ(3 floats)
    struct SamplerParams {
        uint32_t resolution;
        float time;
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    } params = {
        static_cast<uint32_t>(res),
        e->time,
        minX, minY, minZ,
        maxX, maxY, maxZ
    };

    void* data;
    vkMapMemory(e->device, s->paramsMemory, 0, sizeof(params), 0, &data);
    memcpy(data, &params, sizeof(params));
    vkUnmapMemory(e->device, s->paramsMemory);

    // Execute compute shader
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = e->commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s->pipelineLayout, 0, 1, &s->descriptorSet, 0, nullptr);

    uint32_t groupCount = (static_cast<uint32_t>(totalPoints) + 63) / 64;
    vkCmdDispatch(cmdBuffer, groupCount, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(e->graphicsQueue);

    vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmdBuffer);

    // Read back results
    std::vector<float> results(totalPoints);
    vkMapMemory(e->device, s->outputMemory, 0, totalPoints * sizeof(float), 0, &data);
    memcpy(results.data(), data, totalPoints * sizeof(float));
    vkUnmapMemory(e->device, s->outputMemory);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "SDF sampling completed in " << duration.count() << " ms" << std::endl;

    return results;
}

// ============================================================================
// GPU-Based Color Sampling for Mesh Vertex Colors
// ============================================================================

struct ColorSampler {
    bool initialized = false;
    VkBuffer positionBuffer = VK_NULL_HANDLE;
    VkDeviceMemory positionMemory = VK_NULL_HANDLE;
    VkBuffer normalBuffer = VK_NULL_HANDLE;
    VkDeviceMemory normalMemory = VK_NULL_HANDLE;
    VkBuffer colorBuffer = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkBuffer paramsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory paramsMemory = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    size_t maxPoints = 0;
    std::string cachedShaderName;
    time_t cachedShaderModTime = 0;  // Track shader modification for hot reload
};

inline ColorSampler* g_colorSampler = nullptr;

inline ColorSampler* get_color_sampler() {
    if (!g_colorSampler) {
        g_colorSampler = new ColorSampler();
    }
    return g_colorSampler;
}

// Extract scene code for color sampling (includes sceneSDF_mat and getMaterialColor)
inline std::string extract_scene_for_colors(const std::string& shaderSource) {
    std::string result;

    // Find the start of SDF primitives or helper functions
    size_t helperStart = shaderSource.find("// SDF PRIMITIVES");
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("// BOOLEAN OPERATIONS");
    }
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("float opUnion");
    }
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("float sdSphere");
    }
    if (helperStart == std::string::npos) {
        std::cerr << "Could not find SDF helpers in shader" << std::endl;
        return "";
    }

    // Find the end - we want everything up to main() or setCamera()
    size_t endPos = shaderSource.find("void main()");
    if (endPos == std::string::npos) {
        endPos = shaderSource.find("mat3 setCamera");
    }
    if (endPos == std::string::npos) {
        endPos = shaderSource.find("// RAYMARCHING");
    }
    if (endPos == std::string::npos) {
        std::cerr << "Could not find end of extractable code" << std::endl;
        return "";
    }

    // Extract everything from helpers to end (includes SDF, materials, noise, lighting, painterly)
    result = shaderSource.substr(helperStart, endPos - helperStart);

    // Check for required functions and add defaults if missing
    if (result.find("vec3 getMaterialColor(") == std::string::npos) {
        result += R"(

vec3 getMaterialColor(int matID, vec3 p) {
    return vec3(0.8, 0.8, 0.8);
}
)";
    }

    if (result.find("vec2 sceneSDF_mat(") == std::string::npos) {
        result += R"(

vec2 sceneSDF_mat(vec3 p) {
    return vec2(sceneSDF(p), 1.0);
}
)";
    }

    if (result.find("float calcSoftShadow(") == std::string::npos) {
        result += R"(

float calcSoftShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 64 && t < maxt; i++) {
        float h = sceneSDF(ro + rd * t);
        if (h < 0.001) return 0.0;
        res = min(res, k * h / t);
        t += clamp(h, 0.02, 0.1);
    }
    return res;
}
)";
    }

    if (result.find("float calcAO(") == std::string::npos) {
        result += R"(

float calcAO(vec3 pos, vec3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.01 + 0.12 * float(i);
        float d = sceneSDF(pos + h * nor);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}
)";
    }

    // Add noise/painterly defaults if not found
    if (result.find("float hash2D(") == std::string::npos) {
        result += R"(

float hash2D(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
)";
    }

    if (result.find("float noise2D(") == std::string::npos) {
        result += R"(

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2D(i);
    float b = hash2D(i + vec2(1.0, 0.0));
    float c = hash2D(i + vec2(0.0, 1.0));
    float d = hash2D(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
)";
    }

    if (result.find("float fbm(") == std::string::npos) {
        result += R"(

float fbm(vec2 p) {
    float f = 0.0;
    f += 0.5 * noise2D(p); p *= 2.0;
    f += 0.25 * noise2D(p); p *= 2.0;
    f += 0.125 * noise2D(p);
    return f;
}
)";
    }

    if (result.find("vec3 posterize(") == std::string::npos) {
        result += R"(

vec3 posterize(vec3 col, float levels) {
    return floor(col * levels + 0.5) / levels;
}
)";
    }

    if (result.find("float brushStroke(") == std::string::npos) {
        result += R"(

float brushStroke(vec2 uv, vec3 normal, float scale) {
    vec2 dir = normalize(normal.xy + 0.001);
    float angle = atan(dir.y, dir.x);
    vec2 rotUV = vec2(
        uv.x * cos(angle) - uv.y * sin(angle),
        uv.x * sin(angle) + uv.y * cos(angle)
    );
    rotUV.x *= 3.0;
    return fbm(rotUV * scale);
}
)";
    }

    return result;
}

// Build the color sampler shader from template and current scene
inline std::string build_color_sampler_shader(const std::string& sceneShaderPath) {
    auto* e = get_engine();
    std::string templatePath = e->shaderDir + "/color_sampler.comp";
    std::string templateSrc = read_text_file(templatePath);
    if (templateSrc.empty()) {
        std::cerr << "Could not read color sampler template: " << templatePath << std::endl;
        return "";
    }

    std::string sceneSrc = read_text_file(sceneShaderPath);
    if (sceneSrc.empty()) {
        std::cerr << "Could not read scene shader: " << sceneShaderPath << std::endl;
        return "";
    }

    std::string sceneCode = extract_scene_for_colors(sceneSrc);
    if (sceneCode.empty()) {
        std::cerr << "Could not extract scene code for color sampling" << std::endl;
        return "";
    }

    size_t markerStart = templateSrc.find("// MARKER_SCENE_SDF_START");
    size_t markerEnd = templateSrc.find("// MARKER_SCENE_SDF_END");
    if (markerStart == std::string::npos || markerEnd == std::string::npos) {
        std::cerr << "Could not find markers in color sampler template" << std::endl;
        return "";
    }

    markerEnd = templateSrc.find('\n', markerEnd) + 1;
    return templateSrc.substr(0, markerStart) + "\n" + sceneCode + "\n" + templateSrc.substr(markerEnd);
}

inline void cleanup_color_sampler() {
    auto* s = get_color_sampler();
    auto* e = get_engine();
    if (!s->initialized || !e || !e->device) return;

    vkDeviceWaitIdle(e->device);

    if (s->pipeline) vkDestroyPipeline(e->device, s->pipeline, nullptr);
    if (s->pipelineLayout) vkDestroyPipelineLayout(e->device, s->pipelineLayout, nullptr);
    if (s->shaderModule) vkDestroyShaderModule(e->device, s->shaderModule, nullptr);
    if (s->descriptorPool) vkDestroyDescriptorPool(e->device, s->descriptorPool, nullptr);
    if (s->descriptorSetLayout) vkDestroyDescriptorSetLayout(e->device, s->descriptorSetLayout, nullptr);

    if (s->positionBuffer) vkDestroyBuffer(e->device, s->positionBuffer, nullptr);
    if (s->positionMemory) vkFreeMemory(e->device, s->positionMemory, nullptr);
    if (s->normalBuffer) vkDestroyBuffer(e->device, s->normalBuffer, nullptr);
    if (s->normalMemory) vkFreeMemory(e->device, s->normalMemory, nullptr);
    if (s->colorBuffer) vkDestroyBuffer(e->device, s->colorBuffer, nullptr);
    if (s->colorMemory) vkFreeMemory(e->device, s->colorMemory, nullptr);
    if (s->paramsBuffer) vkDestroyBuffer(e->device, s->paramsBuffer, nullptr);
    if (s->paramsMemory) vkFreeMemory(e->device, s->paramsMemory, nullptr);

    s->initialized = false;
    s->maxPoints = 0;
    s->cachedShaderName = "";
}

inline bool init_color_sampler(size_t numPoints) {
    auto* s = get_color_sampler();
    auto* e = get_engine();
    if (!e || !e->initialized) {
        std::cerr << "Engine not initialized" << std::endl;
        return false;
    }

    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    std::string templatePath = e->shaderDir + "/color_sampler.comp";
    time_t sceneModTime = 0;
    time_t templateModTime = 0;
    struct stat st;
    if (stat(shaderPath.c_str(), &st) == 0) {
        sceneModTime = st.st_mtime;
    }
    if (stat(templatePath.c_str(), &st) == 0) {
        templateModTime = st.st_mtime;
    }
    // Use max of both mod times to detect changes in either file
    time_t currentModTime = sceneModTime > templateModTime ? sceneModTime : templateModTime;

    // Check if we can reuse cached sampler (same shader name, same mod time, enough capacity)
    if (s->initialized && s->maxPoints >= numPoints &&
        s->cachedShaderName == e->currentShaderName &&
        s->cachedShaderModTime == currentModTime) {
        return true;
    }

    if (s->initialized) {
        cleanup_color_sampler();
    }

    std::cout << "Initializing color sampler for " << numPoints << " points..." << std::endl;
    std::string samplerSrc = build_color_sampler_shader(shaderPath);
    if (samplerSrc.empty()) {
        return false;
    }

    auto spirv = compile_glsl_to_spirv(samplerSrc, "color_sampler.comp", shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "Failed to compile color sampler shader" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderModuleInfo.pCode = spirv.data();

    if (vkCreateShaderModule(e->device, &shaderModuleInfo, nullptr, &s->shaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create color sampler shader module" << std::endl;
        return false;
    }

    VkDeviceSize positionSize = numPoints * sizeof(float) * 4;
    VkDeviceSize normalSize = numPoints * sizeof(float) * 4;
    VkDeviceSize colorSize = numPoints * sizeof(float) * 4;
    VkDeviceSize paramsSize = 48;  // numPoints(4) + time(4) + padding(8) + cameraPos(16) + lightDir(16)

    auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(e->device, buffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(e->device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
            return false;
        }
        vkBindBufferMemory(e->device, buffer, memory, 0);
        return true;
    };

    if (!createBuffer(positionSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, s->positionBuffer, s->positionMemory)) return false;
    if (!createBuffer(normalSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, s->normalBuffer, s->normalMemory)) return false;
    if (!createBuffer(colorSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, s->colorBuffer, s->colorMemory)) return false;
    if (!createBuffer(paramsSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, s->paramsBuffer, s->paramsMemory)) return false;

    // Create descriptor set layout (4 bindings: positions, normals, colors, params)
    VkDescriptorSetLayoutBinding bindings[4] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(e->device, &layoutInfo, nullptr, &s->descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &s->descriptorPool) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = s->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkAllocateDescriptorSets(e->device, &dsAllocInfo, &s->descriptorSet) != VK_SUCCESS) {
        return false;
    }

    VkDescriptorBufferInfo positionBufferInfo{s->positionBuffer, 0, positionSize};
    VkDescriptorBufferInfo normalBufferInfo{s->normalBuffer, 0, normalSize};
    VkDescriptorBufferInfo colorBufferInfo{s->colorBuffer, 0, colorSize};
    VkDescriptorBufferInfo paramsBufferInfo{s->paramsBuffer, 0, paramsSize};

    VkWriteDescriptorSet writes[4] = {};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &positionBufferInfo, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &normalBufferInfo, nullptr};
    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &colorBufferInfo, nullptr};
    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s->descriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &paramsBufferInfo, nullptr};

    vkUpdateDescriptorSets(e->device, 4, writes, 0, nullptr);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &s->pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = s->shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = s->pipelineLayout;

    if (vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s->pipeline) != VK_SUCCESS) {
        return false;
    }

    s->maxPoints = numPoints;
    s->cachedShaderName = e->currentShaderName;
    s->cachedShaderModTime = currentModTime;  // Track mod time for hot reload
    s->initialized = true;

    std::cout << "Color sampler initialized successfully" << std::endl;
    return true;
}

// Sample colors at mesh vertex positions
inline std::vector<mc::Color3> sample_vertex_colors(const mc::Mesh& mesh) {
    auto* s = get_color_sampler();
    auto* e = get_engine();

    size_t numPoints = mesh.vertices.size();
    if (numPoints == 0) return {};

    // Ensure normals are computed
    if (mesh.normals.size() != numPoints) {
        std::cerr << "Mesh normals not computed" << std::endl;
        return {};
    }

    if (!init_color_sampler(numPoints)) {
        std::cerr << "Failed to initialize color sampler" << std::endl;
        return {};
    }

    std::cout << "Sampling colors for " << numPoints << " vertices on GPU..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    // Upload positions
    std::vector<float> positions(numPoints * 4);
    for (size_t i = 0; i < numPoints; i++) {
        positions[i * 4 + 0] = mesh.vertices[i].x;
        positions[i * 4 + 1] = mesh.vertices[i].y;
        positions[i * 4 + 2] = mesh.vertices[i].z;
        positions[i * 4 + 3] = 0.0f;
    }

    void* data;
    vkMapMemory(e->device, s->positionMemory, 0, positions.size() * sizeof(float), 0, &data);
    memcpy(data, positions.data(), positions.size() * sizeof(float));
    vkUnmapMemory(e->device, s->positionMemory);

    // Upload normals
    std::vector<float> normals(numPoints * 4);
    for (size_t i = 0; i < numPoints; i++) {
        normals[i * 4 + 0] = mesh.normals[i].x;
        normals[i * 4 + 1] = mesh.normals[i].y;
        normals[i * 4 + 2] = mesh.normals[i].z;
        normals[i * 4 + 3] = 0.0f;
    }

    vkMapMemory(e->device, s->normalMemory, 0, normals.size() * sizeof(float), 0, &data);
    memcpy(data, normals.data(), normals.size() * sizeof(float));
    vkUnmapMemory(e->device, s->normalMemory);

    // Upload params (numPoints, time, cameraPos, lightDir)
    struct {
        uint32_t numPoints;
        float time;
        float pad1, pad2;
        float cameraPos[4];
        float lightDir[4];
    } params;

    params.numPoints = static_cast<uint32_t>(numPoints);
    params.time = e->time;
    params.pad1 = 0;
    params.pad2 = 0;

    // Get camera position
    float camPos[3];
    e->camera.getPosition(camPos);
    params.cameraPos[0] = camPos[0];
    params.cameraPos[1] = camPos[1];
    params.cameraPos[2] = camPos[2];
    params.cameraPos[3] = 1.5f;  // fov

    // Match main shader light direction: (1, 2, 1) normalized
    float lightLen = sqrt(1.0f + 4.0f + 1.0f);  // sqrt(6)
    params.lightDir[0] = 1.0f / lightLen;
    params.lightDir[1] = 2.0f / lightLen;
    params.lightDir[2] = 1.0f / lightLen;
    params.lightDir[3] = 0.0f;

    vkMapMemory(e->device, s->paramsMemory, 0, sizeof(params), 0, &data);
    memcpy(data, &params, sizeof(params));
    vkUnmapMemory(e->device, s->paramsMemory);

    // Execute compute shader
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = e->commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s->pipelineLayout, 0, 1, &s->descriptorSet, 0, nullptr);

    uint32_t groupCount = (static_cast<uint32_t>(numPoints) + 63) / 64;
    vkCmdDispatch(cmdBuffer, groupCount, 1, 1);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(e->graphicsQueue);

    vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmdBuffer);

    // Read back colors
    std::vector<float> colorData(numPoints * 4);
    vkMapMemory(e->device, s->colorMemory, 0, colorData.size() * sizeof(float), 0, &data);
    memcpy(colorData.data(), data, colorData.size() * sizeof(float));
    vkUnmapMemory(e->device, s->colorMemory);

    // Convert to mc::Color3
    std::vector<mc::Color3> colors(numPoints);
    for (size_t i = 0; i < numPoints; i++) {
        colors[i].r = colorData[i * 4 + 0];
        colors[i].g = colorData[i * 4 + 1];
        colors[i].b = colorData[i * 4 + 2];
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Color sampling completed in " << duration.count() << " ms" << std::endl;

    return colors;
}

// Export current scene SDF to mesh using GPU sampling + CPU marching cubes
// This is the main function - no code duplication!
inline MeshExportResult export_scene_mesh_gpu(
    const char* filepath,
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    int resolution,
    bool includeColors = false,
    bool includeUVs = false
) {
    MeshExportResult result{false, 0, 0, ""};

    auto* e = get_engine();
    if (!e || !e->initialized) {
        result.message = "Engine not initialized";
        return result;
    }

    std::cout << "Exporting scene mesh via GPU sampling..." << std::endl;
    std::cout << "  Bounds: [" << minX << "," << minY << "," << minZ << "] to ["
              << maxX << "," << maxY << "," << maxZ << "]" << std::endl;
    std::cout << "  Resolution: " << resolution << "x" << resolution << "x" << resolution << std::endl;
    if (includeColors) std::cout << "  Including vertex colors" << std::endl;
    if (includeUVs) std::cout << "  Including UV coordinates" << std::endl;

    // Sample SDF on GPU
    auto distances = sample_sdf_grid(minX, minY, minZ, maxX, maxY, maxZ, resolution);
    if (distances.empty()) {
        result.message = "Failed to sample SDF on GPU";
        return result;
    }

    // Generate mesh using CPU marching cubes
    std::cout << "Running Marching Cubes..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    mc::Vec3 bounds_min{minX, minY, minZ};
    mc::Vec3 bounds_max{maxX, maxY, maxZ};
    auto mesh = mc::generateMesh(distances, resolution, bounds_min, bounds_max);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Marching Cubes completed in " << duration.count() << " ms" << std::endl;

    if (mesh.vertices.empty()) {
        result.message = "No surface found in bounds";
        return result;
    }

    // Compute normals (always useful for rendering)
    mc::computeNormals(mesh);

    // Compute UVs if requested
    if (includeUVs) {
        mc::computeUVs(mesh);
    }

    // Sample colors from GPU if requested
    if (includeColors) {
        auto colors = sample_vertex_colors(mesh);
        if (!colors.empty()) {
            mesh.colors = std::move(colors);
            std::cout << "  Applied GPU-sampled vertex colors" << std::endl;
        } else {
            // Fallback to default gray
            mc::setUniformColor(mesh, 0.8f, 0.8f, 0.8f);
            std::cout << "  Using fallback uniform color" << std::endl;
        }
    }

    result.vertices = mesh.vertices.size();
    result.triangles = mesh.indices.size() / 3;

    // Export based on file extension
    std::string path(filepath);
    bool isGLB = path.size() > 4 && (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 5) == ".gltf");

    bool exportSuccess = false;
    if (isGLB) {
        exportSuccess = mc::exportGLB(filepath, mesh, includeColors);
    } else {
        exportSuccess = mc::exportOBJ(filepath, mesh, includeColors, includeUVs);
    }

    if (exportSuccess) {
        result.success = true;
        result.message = "Export successful";
        std::cout << "Exported to " << filepath << std::endl;
        std::cout << "  Vertices: " << result.vertices << std::endl;
        std::cout << "  Triangles: " << result.triangles << std::endl;
    } else {
        result.message = isGLB ? "Failed to write GLB file" : "Failed to write OBJ file";
    }

    return result;
}

// Convenience wrapper with default bounds
// Convenience wrapper - uses stored mesh if available at matching resolution
inline MeshExportResult export_scene_mesh_gpu(
    const char* filepath, 
    int resolution = 64,
    bool includeColors = false,
    bool includeUVs = false
) {
    auto* e = get_engine();
    MeshExportResult result{false, 0, 0, ""};

    if (!e || !e->initialized) {
        result.message = "Engine not initialized";
        return result;
    }

    // If we have a stored mesh at the requested resolution, use it
    if (e->currentMeshResolution == resolution && !e->currentMesh.vertices.empty()) {
        std::cout << "Exporting stored mesh (resolution " << resolution << ")..." << std::endl;
        if (includeColors) std::cout << "  Including vertex colors" << std::endl;
        if (includeUVs) std::cout << "  Including UV coordinates" << std::endl;

        // Make a copy to add normals/colors/UVs if needed
        mc::Mesh exportMesh = e->currentMesh;

        // Compute normals if not already present
        if (!exportMesh.hasNormals()) {
            mc::computeNormals(exportMesh);
        }

        // Compute UVs if requested and not present
        if (includeUVs && !exportMesh.hasUVs()) {
            mc::computeUVs(exportMesh);
        }

        // Sample colors from GPU if requested
        if (includeColors) {
            auto colors = sample_vertex_colors(exportMesh);
            if (!colors.empty()) {
                exportMesh.colors = std::move(colors);
                std::cout << "  Applied GPU-sampled vertex colors" << std::endl;
            } else {
                mc::setUniformColor(exportMesh, 0.8f, 0.8f, 0.8f);
                std::cout << "  Using fallback uniform color" << std::endl;
            }
        }

        result.vertices = exportMesh.vertices.size();
        result.triangles = exportMesh.indices.size() / 3;

        // Export based on file extension
        std::string path(filepath);
        bool isGLB = path.size() > 4 && (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 5) == ".gltf");

        bool exportSuccess = false;
        if (isGLB) {
            exportSuccess = mc::exportGLB(filepath, exportMesh, includeColors);
        } else {
            exportSuccess = mc::exportOBJ(filepath, exportMesh, includeColors, includeUVs);
        }

        if (exportSuccess) {
            result.success = true;
            result.message = "Export successful";
            std::cout << "Exported to " << filepath << std::endl;
            std::cout << "  Vertices: " << result.vertices << std::endl;
            std::cout << "  Triangles: " << result.triangles << std::endl;
        } else {
            result.message = isGLB ? "Failed to write GLB file" : "Failed to write OBJ file";
        }
        return result;
    }

    // Otherwise, regenerate mesh at requested resolution
    return export_scene_mesh_gpu(filepath, -2.0f, -2.0f, -2.0f, 2.0f, 2.0f, 2.0f, 
                                  resolution, includeColors, includeUVs);
}

// ============================================================================
// Mesh Preview Rendering System
// ============================================================================

// Mesh vertex structure (position + normal)
struct MeshVertex {
    float pos[3];
    float normal[3];
    float color[3];
};;

inline void cleanup_mesh_preview() {
    auto* e = get_engine();
    if (!e || !e->device) return;

    vkDeviceWaitIdle(e->device);

    if (e->meshVertexBuffer) vkDestroyBuffer(e->device, e->meshVertexBuffer, nullptr);
    if (e->meshVertexMemory) vkFreeMemory(e->device, e->meshVertexMemory, nullptr);
    if (e->meshIndexBuffer) vkDestroyBuffer(e->device, e->meshIndexBuffer, nullptr);
    if (e->meshIndexMemory) vkFreeMemory(e->device, e->meshIndexMemory, nullptr);
    if (e->meshPipeline) vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
    if (e->meshPipelineLayout) vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
    if (e->meshDescriptorPool) vkDestroyDescriptorPool(e->device, e->meshDescriptorPool, nullptr);
    if (e->meshDescriptorSetLayout) vkDestroyDescriptorSetLayout(e->device, e->meshDescriptorSetLayout, nullptr);

    e->meshVertexBuffer = VK_NULL_HANDLE;
    e->meshVertexMemory = VK_NULL_HANDLE;
    e->meshIndexBuffer = VK_NULL_HANDLE;
    e->meshIndexMemory = VK_NULL_HANDLE;
    e->meshPipeline = VK_NULL_HANDLE;
    e->meshPipelineLayout = VK_NULL_HANDLE;
    e->meshDescriptorPool = VK_NULL_HANDLE;
    e->meshDescriptorSetLayout = VK_NULL_HANDLE;
    e->meshDescriptorSet = VK_NULL_HANDLE;
    e->meshIndexCount = 0;
    e->meshVertexCount = 0;
    e->meshPipelineInitialized = false;
}

inline bool init_mesh_pipeline() {
    auto* e = get_engine();
    if (!e || !e->initialized) return false;
    if (e->meshPipelineInitialized) return true;

    std::cout << "Initializing mesh preview pipeline..." << std::endl;

    // Load mesh shaders
    auto vertCode = read_file(e->shaderDir + "/mesh.vert.spv");
    auto fragCode = read_file(e->shaderDir + "/mesh.frag.spv");

    if (vertCode.empty() || fragCode.empty()) {
        std::cerr << "Failed to load mesh shaders" << std::endl;
        return false;
    }

    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo vertInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vertInfo.codeSize = vertCode.size();
    vertInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    if (vkCreateShaderModule(e->device, &vertInfo, nullptr, &vertModule) != VK_SUCCESS) {
        std::cerr << "Failed to create mesh vertex shader module" << std::endl;
        return false;
    }

    VkShaderModuleCreateInfo fragInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fragInfo.codeSize = fragCode.size();
    fragInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    if (vkCreateShaderModule(e->device, &fragInfo, nullptr, &fragModule) != VK_SUCCESS) {
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        std::cerr << "Failed to create mesh fragment shader module" << std::endl;
        return false;
    }

    // Create descriptor set layout (uniform buffer for camera)
    VkDescriptorSetLayoutBinding uboBinding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;

    if (vkCreateDescriptorSetLayout(e->device, &layoutInfo, nullptr, &e->meshDescriptorSetLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        vkDestroyShaderModule(e->device, fragModule, nullptr);
        return false;
    }

    // Create descriptor pool
    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &e->meshDescriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(e->device, e->meshDescriptorSetLayout, nullptr);
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        vkDestroyShaderModule(e->device, fragModule, nullptr);
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsAllocInfo.descriptorPool = e->meshDescriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &e->meshDescriptorSetLayout;

    if (vkAllocateDescriptorSets(e->device, &dsAllocInfo, &e->meshDescriptorSet) != VK_SUCCESS) {
        vkDestroyDescriptorPool(e->device, e->meshDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->meshDescriptorSetLayout, nullptr);
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        vkDestroyShaderModule(e->device, fragModule, nullptr);
        return false;
    }

    // Update descriptor set to point to the uniform buffer
    VkDescriptorBufferInfo bufferInfo{e->uniformBuffer, 0, sizeof(UBO)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = e->meshDescriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &e->meshDescriptorSetLayout;

    if (vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->meshPipelineLayout) != VK_SUCCESS) {
        vkDestroyDescriptorPool(e->device, e->meshDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->meshDescriptorSetLayout, nullptr);
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        vkDestroyShaderModule(e->device, fragModule, nullptr);
        return false;
    }

    // Create graphics pipeline
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Vertex input: position (vec3) + normal (vec3) + color (vec3)
    VkVertexInputBindingDescription bindingDesc{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrDescs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, color)}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, 0, (float)g_framebufferWidth, (float)g_framebufferHeight, 0, 1};
    VkRect2D scissor = {{0, 0}, {g_framebufferWidth, g_framebufferHeight}};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // Solid mode for proper preview
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;  // Backface culling for solid mesh
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth testing for proper solid mesh rendering
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // No blending for solid mesh - opaque rendering
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = e->meshPipelineLayout;
    pipelineInfo.renderPass = e->renderPass;

    if (vkCreateGraphicsPipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &e->meshPipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyDescriptorPool(e->device, e->meshDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->meshDescriptorSetLayout, nullptr);
        vkDestroyShaderModule(e->device, vertModule, nullptr);
        vkDestroyShaderModule(e->device, fragModule, nullptr);
        std::cerr << "Failed to create mesh pipeline" << std::endl;
        return false;
    }

    vkDestroyShaderModule(e->device, vertModule, nullptr);
    vkDestroyShaderModule(e->device, fragModule, nullptr);

    e->meshPipelineInitialized = true;
    std::cout << "Mesh preview pipeline initialized" << std::endl;
    return true;
}

// Upload mesh data to GPU buffers
inline bool upload_mesh_preview(const mc::Mesh& mesh, const std::vector<mc::Color3>& colors = {}) {
    auto* e = get_engine();
    if (!e || !e->initialized) return false;

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        std::cerr << "Empty mesh data" << std::endl;
        return false;
    }

    // Destroy old buffers
    if (e->meshVertexBuffer) {
        vkDeviceWaitIdle(e->device);
        vkDestroyBuffer(e->device, e->meshVertexBuffer, nullptr);
        vkFreeMemory(e->device, e->meshVertexMemory, nullptr);
        e->meshVertexBuffer = VK_NULL_HANDLE;
        e->meshVertexMemory = VK_NULL_HANDLE;
    }
    if (e->meshIndexBuffer) {
        vkDestroyBuffer(e->device, e->meshIndexBuffer, nullptr);
        vkFreeMemory(e->device, e->meshIndexMemory, nullptr);
        e->meshIndexBuffer = VK_NULL_HANDLE;
        e->meshIndexMemory = VK_NULL_HANDLE;
    }

    // Compute normals for each vertex (average of face normals)
    std::vector<mc::Vec3> normals(mesh.vertices.size(), {0, 0, 0});
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        mc::Vec3 v0 = mesh.vertices[i0];
        mc::Vec3 v1 = mesh.vertices[i1];
        mc::Vec3 v2 = mesh.vertices[i2];

        // Edge vectors
        mc::Vec3 e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
        mc::Vec3 e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};

        // Cross product for face normal
        mc::Vec3 n = {
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x
        };

        normals[i0].x += n.x; normals[i0].y += n.y; normals[i0].z += n.z;
        normals[i1].x += n.x; normals[i1].y += n.y; normals[i1].z += n.z;
        normals[i2].x += n.x; normals[i2].y += n.y; normals[i2].z += n.z;
    }

    // Normalize
    for (auto& n : normals) {
        float len = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0001f) {
            n.x /= len; n.y /= len; n.z /= len;
        }
    }

    // Check if we have valid colors
    bool hasColors = !colors.empty() && colors.size() == mesh.vertices.size();

    // Create interleaved vertex data
    std::vector<MeshVertex> vertices(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        vertices[i].pos[0] = mesh.vertices[i].x;
        vertices[i].pos[1] = mesh.vertices[i].y;
        vertices[i].pos[2] = mesh.vertices[i].z;
        vertices[i].normal[0] = normals[i].x;
        vertices[i].normal[1] = normals[i].y;
        vertices[i].normal[2] = normals[i].z;
        if (hasColors) {
            vertices[i].color[0] = colors[i].r;
            vertices[i].color[1] = colors[i].g;
            vertices[i].color[2] = colors[i].b;
        } else {
            vertices[i].color[0] = 0.0f;
            vertices[i].color[1] = 0.0f;
            vertices[i].color[2] = 0.0f;
        }
    }

    VkDeviceSize vertexBufferSize = sizeof(MeshVertex) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * mesh.indices.size();

    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = vertexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &e->meshVertexBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create vertex buffer" << std::endl;
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, e->meshVertexBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &e->meshVertexMemory) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, e->meshVertexBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(e->device, e->meshVertexBuffer, e->meshVertexMemory, 0);

    // Upload vertex data
    void* data;
    vkMapMemory(e->device, e->meshVertexMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertices.data(), vertexBufferSize);
    vkUnmapMemory(e->device, e->meshVertexMemory);

    // Create index buffer
    bufferInfo.size = indexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &e->meshIndexBuffer) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, e->meshVertexBuffer, nullptr);
        vkFreeMemory(e->device, e->meshVertexMemory, nullptr);
        return false;
    }

    vkGetBufferMemoryRequirements(e->device, e->meshIndexBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &e->meshIndexMemory) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, e->meshVertexBuffer, nullptr);
        vkFreeMemory(e->device, e->meshVertexMemory, nullptr);
        vkDestroyBuffer(e->device, e->meshIndexBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(e->device, e->meshIndexBuffer, e->meshIndexMemory, 0);

    // Upload index data
    vkMapMemory(e->device, e->meshIndexMemory, 0, indexBufferSize, 0, &data);
    memcpy(data, mesh.indices.data(), indexBufferSize);
    vkUnmapMemory(e->device, e->meshIndexMemory);

    e->meshVertexCount = static_cast<uint32_t>(vertices.size());
    e->meshIndexCount = static_cast<uint32_t>(mesh.indices.size());

    std::cout << "Mesh preview uploaded: " << e->meshVertexCount << " vertices, "
              << (e->meshIndexCount / 3) << " triangles" << std::endl;
    return true;
}

// Generate mesh from current scene and upload to GPU
inline bool generate_mesh_preview(int resolution = -1) {
    auto* e = get_engine();
    if (!e || !e->initialized) return false;

    if (resolution <= 0) {
        resolution = e->meshPreviewResolution;
    }

    std::cout << "Generating mesh preview at resolution " << resolution << "..." << std::endl;

    // Sample SDF on GPU
    auto distances = sample_sdf_grid(-2.0f, -2.0f, -2.0f, 2.0f, 2.0f, 2.0f, resolution);
    if (distances.empty()) {
        std::cerr << "Failed to sample SDF" << std::endl;
        return false;
    }

    // Generate mesh using CPU marching cubes or dual contouring
    mc::Vec3 bounds_min{-2.0f, -2.0f, -2.0f};
    mc::Vec3 bounds_max{2.0f, 2.0f, 2.0f};
    if (e->meshUseDualContouring) {
        e->currentMesh = mc::generateMeshDC(distances, resolution, bounds_min, bounds_max);
    } else {
        e->currentMesh = mc::generateMesh(distances, resolution, bounds_min, bounds_max);
        std::cout << "MC mesh: " << e->currentMesh.vertices.size() << " vertices, "
                  << (e->currentMesh.indices.size() / 3) << " triangles" << std::endl;
    }
    e->currentMeshResolution = resolution;

    if (e->currentMesh.vertices.empty()) {
        std::cerr << "No surface found" << std::endl;
        return false;
    }

    // Initialize pipeline if needed
    if (!e->meshPipelineInitialized) {
        if (!init_mesh_pipeline()) {
            return false;
        }
    }

    // Sample vertex colors if enabled
    std::vector<mc::Color3> colors;
    if (e->meshUseVertexColors) {
        std::cout << "Sampling vertex colors..." << std::endl;
        // Compute normals (required for color sampling)
        mc::computeNormals(e->currentMesh);
        colors = sample_vertex_colors(e->currentMesh);
        if (colors.empty()) {
            std::cerr << "Warning: Failed to sample colors, using default" << std::endl;
        } else {
            std::cout << "Sampled " << colors.size() << " vertex colors" << std::endl;
        }
    }

    // Upload to GPU
    return upload_mesh_preview(e->currentMesh, colors);
}

// Render mesh preview (called from draw_frame)
inline void render_mesh_preview(VkCommandBuffer cmd) {
    auto* e = get_engine();
    if (!e || !e->meshPreviewVisible || !e->meshPipelineInitialized ||
        e->meshIndexCount == 0 || !e->meshVertexBuffer || !e->meshIndexBuffer) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipelineLayout,
                           0, 1, &e->meshDescriptorSet, 0, nullptr);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &e->meshVertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, e->meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, e->meshIndexCount, 1, 0, 0, 0);
}

// API functions for jank
inline bool get_mesh_preview_visible() {
    auto* e = get_engine();
    return e ? e->meshPreviewVisible : false;
}

inline void set_mesh_preview_visible(bool visible) {
    auto* e = get_engine();
    if (!e) return;
    e->meshPreviewVisible = visible;
    e->dirty = true;
}

inline bool get_mesh_render_solid() {
    auto* e = get_engine();
    return e ? e->meshRenderSolid : true;
}

inline void set_mesh_render_solid(bool solid) {
    auto* e = get_engine();
    if (!e) return;
    if (e->meshRenderSolid != solid) {
        e->meshRenderSolid = solid;
        e->dirty = true;
    }
}

inline float get_mesh_scale() {
    auto* e = get_engine();
    return e ? e->meshScale : 1.0f;
}

inline void set_mesh_scale(float scale) {
    auto* e = get_engine();
    if (!e) return;
    if (e->meshScale != scale) {
        e->meshScale = scale;
        e->dirty = true;
    }
}

inline bool get_mesh_use_vertex_colors() {
    auto* e = get_engine();
    return e ? e->meshUseVertexColors : false;
}

inline void set_mesh_use_vertex_colors(bool useColors) {
    auto* e = get_engine();
    if (!e) return;
    if (e->meshUseVertexColors != useColors) {
        e->meshUseVertexColors = useColors;
        e->meshNeedsRegenerate = true;  // Need to regenerate with/without colors
        e->dirty = true;
    }
}

inline bool get_mesh_use_dual_contouring() {
    auto* e = get_engine();
    return e ? e->meshUseDualContouring : false;
}

inline void set_mesh_use_dual_contouring(bool useDC) {
    auto* e = get_engine();
    if (!e) return;
    if (e->meshUseDualContouring != useDC) {
        e->meshUseDualContouring = useDC;
        e->meshNeedsRegenerate = true;  // Need to regenerate with different algorithm
        e->dirty = true;
    }
}

inline int get_mesh_preview_resolution() {
    auto* e = get_engine();
    return e ? e->meshPreviewResolution : 64;
}

inline void set_mesh_preview_resolution(int res) {
    auto* e = get_engine();
    if (!e) return;
    e->meshPreviewResolution = std::max(8, res);  // No upper limit!
    e->meshNeedsRegenerate = true;
}

inline uint32_t get_mesh_preview_vertex_count() {
    auto* e = get_engine();
    return e ? e->meshVertexCount : 0;
}

inline uint32_t get_mesh_preview_triangle_count() {
    auto* e = get_engine();
    return e ? (e->meshIndexCount / 3) : 0;
}

inline bool mesh_preview_needs_regenerate() {
    auto* e = get_engine();
    return e ? e->meshNeedsRegenerate : false;
}

inline void clear_mesh_regenerate_flag() {
    auto* e = get_engine();
    if (e) e->meshNeedsRegenerate = false;
}

// Toggle mesh preview with auto-generation
inline void toggle_mesh_preview() {
    auto* e = get_engine();
    if (!e) return;

    e->meshPreviewVisible = !e->meshPreviewVisible;

    if (e->meshPreviewVisible && e->meshIndexCount == 0) {
        // Generate mesh if none exists
        generate_mesh_preview();
    }

    e->dirty = true;
}

} // namespace sdfx
