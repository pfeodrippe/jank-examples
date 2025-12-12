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

// Event recording/replay for automated testing
struct RecordedEvent {
    uint64_t timestamp;
    SDL_Event event;
};
inline std::vector<RecordedEvent>* g_recordedEvents = nullptr;
inline bool g_recording = false;
inline bool g_replaying = false;
inline size_t g_replayIndex = 0;
inline uint64_t g_recordStartTime = 0;
inline uint64_t g_replayStartTime = 0;

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

// ODR-safe global state
static Engine** g_engine_ptr = nullptr;

inline Engine*& get_engine() {
    if (!g_engine_ptr) {
        g_engine_ptr = new Engine*(nullptr);
    }
    return *g_engine_ptr;
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

inline void handle_scroll(float dx, float dy) {
    auto* e = get_engine();
    if (!e) return;
    e->camera.update(0, 0, dy);
    e->dirty = true;
}

// Forward declarations
inline void reload_shader();
inline void check_shader_reload();
inline std::vector<char> read_file(const std::string& filename);

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
inline void switch_shader(int direction);

inline void handle_key_down(SDL_Keycode key) {
    auto* e = get_engine();
    if (!e) return;

    switch (key) {
        case SDLK_ESCAPE:
            e->running = false;
            break;
        case SDLK_R:
            std::cout << "Reloading shader..." << std::endl;
            reload_shader();
            break;
        case SDLK_E:
            e->editMode = !e->editMode;
            e->dirty = true;
            if (e->editMode) {
                std::cout << "EDIT MODE ON - Press 1-5 to select objects, drag gizmo to move" << std::endl;
            } else {
                std::cout << "EDIT MODE OFF" << std::endl;
                e->selectedObject = -1;
            }
            break;
        case SDLK_LEFT:
            e->pendingShaderSwitch = -1;  // Request previous shader (consumed by Jank)
            break;
        case SDLK_RIGHT:
            e->pendingShaderSwitch = 1;   // Request next shader (consumed by Jank)
            break;
        default:
            if (e->editMode && key >= SDLK_1 && key <= SDLK_5) {
                select_object(e, key - SDLK_1 + 1);
            }
            break;
    }
}

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
    std::string spvPath = e->shaderDir + "/" + name + ".spv";
    std::cout << "[DEBUG] load_shader_by_name: compPath=" << compPath << std::endl;
    std::cout << "[DEBUG] load_shader_by_name: spvPath=" << spvPath << std::endl;

    // Compile shader
    std::string compCmd = "glslangValidator -V " + compPath + " -o " + spvPath;
    std::cout << "[DEBUG] load_shader_by_name: compiling..." << std::endl;
    int result = std::system(compCmd.c_str());
    if (result != 0) {
        std::cerr << "[DEBUG] load_shader_by_name: compile FAILED! result=" << result << std::endl;
        return;
    }
    std::cout << "[DEBUG] load_shader_by_name: compile SUCCESS" << std::endl;

    // Destroy old pipeline and shader module
    vkDestroyPipeline(e->device, e->computePipeline, nullptr);
    vkDestroyPipelineLayout(e->device, e->computePipelineLayout, nullptr);
    vkDestroyShaderModule(e->device, e->computeShaderModule, nullptr);

    // Load new shader
    auto code = read_file(spvPath);
    if (code.empty()) {
        std::cerr << "Failed to read shader: " << spvPath << std::endl;
        return;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
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

inline void switch_shader(int direction) {
    auto* e = get_engine();
    if (!e || e->shaderList.empty()) {
        // First time - scan for shaders
        scan_shaders();
    }
    if (!e || e->shaderList.empty()) return;

    int newIndex = e->currentShaderIndex + direction;
    // Wrap around
    if (newIndex < 0) newIndex = static_cast<int>(e->shaderList.size()) - 1;
    if (newIndex >= static_cast<int>(e->shaderList.size())) newIndex = 0;

    e->currentShaderIndex = newIndex;
    std::cout << "Switching to shader [" << newIndex << "]: " << e->shaderList[newIndex] << std::endl;
    load_shader_by_name(e->shaderList[newIndex]);
}

// Main event processing function
inline void process_event(const SDL_Event& event) {
    auto* e = get_engine();
    if (!e) return;

    // Let ImGui process first
    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type) {
        case SDL_EVENT_QUIT:
            e->running = false;
            break;

        case SDL_EVENT_KEY_DOWN:
            if (ImGui::GetIO().WantCaptureKeyboard && event.key.key != SDLK_ESCAPE) break;
            // Undo/Redo: Cmd+Z / Cmd+Shift+Z (macOS)
            if (event.key.key == SDLK_Z && (event.key.mod & SDL_KMOD_GUI)) {
                if (event.key.mod & SDL_KMOD_SHIFT) e->redoRequested = true;
                else e->undoRequested = true;
                break;
            }
            // Duplicate: Cmd+D
            if (event.key.key == SDLK_D && (event.key.mod & SDL_KMOD_GUI)) {
                e->duplicateRequested = true;
                break;
            }
            // Delete: Backspace or Delete key
            if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_DELETE) {
                e->deleteRequested = true;
                break;
            }
            // Reset Transform: Cmd+0
            if (event.key.key == SDLK_0 && (event.key.mod & SDL_KMOD_GUI)) {
                e->resetTransformRequested = true;
                break;
            }
            handle_key_down(event.key.key);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ImGui::GetIO().WantCaptureMouse) break;
            handle_mouse_button(event.button.button, true, event.button.x, event.button.y);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ImGui::GetIO().WantCaptureMouse) break;
            handle_mouse_button(event.button.button, false, event.button.x, event.button.y);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (ImGui::GetIO().WantCaptureMouse) {
                e->lastMouseX = event.motion.x;
                e->lastMouseY = event.motion.y;
                break;
            }
            handle_mouse_motion(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (ImGui::GetIO().WantCaptureMouse) break;
            handle_scroll(event.wheel.x, event.wheel.y);
            break;
    }
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

    // Render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = e->swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    vkCreateRenderPass(e->device, &renderPassInfo, nullptr, &e->renderPass);

    // Framebuffers
    e->framebuffers.resize(e->swapchainImageViews.size());
    for (size_t i = 0; i < e->swapchainImageViews.size(); i++) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = e->renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &e->swapchainImageViews[i];
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

// Forward declaration for replay
inline void process_replay_events();

inline void poll_events() {
    auto* e = get_engine();
    if (!e) return;

    // Process any pending replay events (injected via SDL_PushEvent)
    process_replay_events();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Let ImGui process the event first
        ImGui_ImplSDL3_ProcessEvent(&event);

        // Record event if recording is enabled
        if (g_recording && g_recordedEvents) {
            uint64_t now = SDL_GetTicks();
            g_recordedEvents->push_back({now - g_recordStartTime, event});
        }

        // Process the event for our application
        process_event(event);
    }
}

inline void update_uniforms(double dt) {
    auto* e = get_engine();
    if (!e) return;

    // Check for shader file changes
    check_shader_reload();

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

inline void draw_frame() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    vkWaitForFences(e->device, 1, &e->inFlightFences[e->currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(e->device, e->swapchain, UINT64_MAX,
                          e->imageAvailableSemaphores[e->currentFrame], VK_NULL_HANDLE, &imageIndex);

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

    // Render pass
    VkClearValue clearColor = {{{0, 0, 0, 1}}};
    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = e->renderPass;
    renderPassInfo.framebuffer = e->framebuffers[imageIndex];
    renderPassInfo.renderArea = {{0, 0}, {g_framebufferWidth, g_framebufferHeight}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->graphicsPipelineLayout,
                           0, 1, &e->blitDescriptorSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

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

inline void reload_shader() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    vkDeviceWaitIdle(e->device);

    // Read GLSL source and compile to SPIR-V in memory (no file writes!)
    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    std::string glslSource = read_text_file(shaderPath);
    if (glslSource.empty()) {
        std::cerr << "Failed to read shader source: " << shaderPath << std::endl;
        return;
    }

    auto spirv = compile_glsl_to_spirv(glslSource, (e->currentShaderName + ".comp").c_str(), shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "Shader compilation failed!" << std::endl;
        return;
    }

    // Destroy old pipeline and shader module
    vkDestroyPipeline(e->device, e->computePipeline, nullptr);
    vkDestroyPipelineLayout(e->device, e->computePipelineLayout, nullptr);
    vkDestroyShaderModule(e->device, e->computeShaderModule, nullptr);

    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderModuleInfo.pCode = spirv.data();

    if (vkCreateShaderModule(e->device, &shaderModuleInfo, nullptr, &e->computeShaderModule) != VK_SUCCESS) {
        std::cerr << "Shader module creation failed" << std::endl;
        return;
    }

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

    e->dirty = true;
    std::cout << "Shader reloaded (in-memory compilation)!" << std::endl;
}

inline void check_shader_reload() {
    auto* e = get_engine();
    if (!e || !e->initialized) return;

    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    struct stat st;
    if (stat(shaderPath.c_str(), &st) == 0) {
        if (e->lastShaderModTime != 0 && st.st_mtime != e->lastShaderModTime) {
            std::cout << "Shader file changed, reloading..." << std::endl;
            reload_shader();
        }
        e->lastShaderModTime = st.st_mtime;
    }
}

inline void set_camera(double dist, double ax, double ay) {
    auto* e = get_engine();
    if (!e) return;
    e->camera.distance = static_cast<float>(dist);
    e->camera.angleX = static_cast<float>(ax);
    e->camera.angleY = static_cast<float>(ay);
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

inline void set_edit_mode(bool enabled) {
    auto* e = get_engine();
    if (e) e->editMode = enabled;
}

inline int get_selected_object() {
    auto* e = get_engine();
    return e ? e->selectedObject : -1;
}

inline void set_selected_object(int id) {
    auto* e = get_engine();
    if (e) e->selectedObject = id;
}

inline int get_hovered_axis() {
    auto* e = get_engine();
    return e ? e->hoveredAxis : -1;
}

inline int get_dragging_axis() {
    auto* e = get_engine();
    return e ? e->draggingAxis : -1;
}

// Keyboard request accessors (consume = check and clear)
inline bool consume_undo_request() {
    auto* e = get_engine();
    if (e && e->undoRequested) { e->undoRequested = false; return true; }
    return false;
}
inline bool consume_redo_request() {
    auto* e = get_engine();
    if (e && e->redoRequested) { e->redoRequested = false; return true; }
    return false;
}
inline bool consume_duplicate_request() {
    auto* e = get_engine();
    if (e && e->duplicateRequested) { e->duplicateRequested = false; return true; }
    return false;
}
inline bool consume_delete_request() {
    auto* e = get_engine();
    if (e && e->deleteRequested) { e->deleteRequested = false; return true; }
    return false;
}
inline bool consume_reset_transform_request() {
    auto* e = get_engine();
    if (e && e->resetTransformRequested) { e->resetTransformRequested = false; return true; }
    return false;
}

inline bool is_dirty() {
    auto* e = get_engine();
    return e ? e->dirty : false;
}

// Continuous rendering mode API
inline bool get_continuous_mode() {
    auto* e = get_engine();
    return e ? e->continuousMode : false;
}

inline void set_continuous_mode(bool enabled) {
    auto* e = get_engine();
    if (e) {
        e->continuousMode = enabled;
        if (enabled) e->dirty = true;  // Start rendering immediately
    }
}

inline void set_gizmo_pos(double x, double y, double z) {
    auto* e = get_engine();
    if (e) {
        e->selectedPos[0] = static_cast<float>(x);
        e->selectedPos[1] = static_cast<float>(y);
        e->selectedPos[2] = static_cast<float>(z);
    }
}

inline void get_mouse_pos(double* x, double* y) {
    auto* e = get_engine();
    if (e && e->window) {
        float fx, fy;
        SDL_GetMouseState(&fx, &fy);
        *x = (double)fx;
        *y = (double)fy;
    }
}

inline bool is_mouse_pressed() {
    auto* e = get_engine();
    return e ? e->mousePressed : false;
}

// ============================================================================
// ImGui API for Jank
// ============================================================================

inline void imgui_new_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

inline void imgui_render() {
    ImGui::Render();
}

inline void imgui_render_draw_data(VkCommandBuffer cmd) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

inline void imgui_set_next_window_pos(float x, float y) {
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
}

inline void imgui_set_next_window_size(float w, float h) {
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
}

inline void imgui_begin(const char* name) {
    ImGui::Begin(name);
}

inline void imgui_end() {
    ImGui::End();
}

inline void imgui_text(const char* text) {
    ImGui::Text("%s", text);
}

inline void imgui_separator() {
    ImGui::Separator();
}

inline bool imgui_button(const char* label) {
    return ImGui::Button(label);
}

inline bool imgui_drag_float(const char* label, float* v, float speed) {
    return ImGui::DragFloat(label, v, speed);
}

inline bool imgui_drag_float3(const char* label, float* v, float speed) {
    return ImGui::DragFloat3(label, v, speed);
}

inline bool imgui_slider_float(const char* label, float* v, float min, float max) {
    return ImGui::SliderFloat(label, v, min, max);
}

inline bool imgui_checkbox(const char* label, bool* v) {
    return ImGui::Checkbox(label, v);
}

inline void imgui_same_line() {
    ImGui::SameLine();
}

inline void set_dirty() {
    auto* e = get_engine();
    if (e) e->dirty = true;
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

// Pending shader switch - consumed by Jank main loop
inline int get_pending_shader_switch() {
    auto* e = get_engine();
    return e ? e->pendingShaderSwitch : 0;
}

inline void clear_pending_shader_switch() {
    auto* e = get_engine();
    if (e) e->pendingShaderSwitch = 0;
}

inline float* get_object_position(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) {
        return e->objects[idx].position;
    }
    return nullptr;
}

inline float* get_object_rotation(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) {
        return e->objects[idx].rotation;
    }
    return nullptr;
}

// Helper functions for Jank to set position/rotation
inline void set_object_position(int idx, float x, float y, float z) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) {
        e->objects[idx].position[0] = x;
        e->objects[idx].position[1] = y;
        e->objects[idx].position[2] = z;
    }
}
inline void set_object_rotation(int idx, float rx, float ry, float rz) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) {
        e->objects[idx].rotation[0] = rx;
        e->objects[idx].rotation[1] = ry;
        e->objects[idx].rotation[2] = rz;
    }
}

// Game-specific: Set object position with 4 components (x, y, z, w)
// The w component can be used for rotation or other game-specific data
// This directly modifies the objects array which gets copied to UBO
inline void set_object_position_4(int idx, float x, float y, float z, float w) {
    auto* e = get_engine();
    if (!e) return;

    // Ensure we have enough objects
    while (idx >= (int)e->objects.size() && e->objects.size() < MAX_OBJECTS) {
        e->objects.push_back(SceneObject(0, 0, 0, 0, false));
    }

    if (idx >= 0 && idx < (int)e->objects.size()) {
        e->objects[idx].position[0] = x;
        e->objects[idx].position[1] = y;
        e->objects[idx].position[2] = z;
        // Store w in rotation[0] since type is int and can't store float rotation
        e->objects[idx].rotation[0] = w;
    }
}

// Duplicate object - returns new object index or -1 on failure
inline int duplicate_object(int idx) {
    auto* e = get_engine();
    if (!e || idx < 0 || idx >= (int)e->objects.size()) return -1;
    if (e->objects.size() >= MAX_OBJECTS) return -1;

    auto& src = e->objects[idx];
    SceneObject copy(src.position[0], src.position[1], src.position[2], src.type, src.selectable);
    copy.rotation[0] = src.rotation[0];
    copy.rotation[1] = src.rotation[1];
    copy.rotation[2] = src.rotation[2];
    e->objects.push_back(copy);
    e->dirty = true;
    return (int)e->objects.size() - 1;
}

// Delete object - returns true on success
inline bool delete_object(int idx) {
    auto* e = get_engine();
    if (!e || idx <= 0 || idx >= (int)e->objects.size()) return false;  // Can't delete ground (idx 0)

    e->objects.erase(e->objects.begin() + idx);
    if (e->selectedObject >= idx) e->selectedObject = -1;  // Deselect if deleted or after
    e->dirty = true;
    return true;
}

// Reset object transform to origin
inline void reset_object_transform(int idx) {
    auto* e = get_engine();
    if (!e || idx < 0 || idx >= (int)e->objects.size()) return;

    e->objects[idx].position[0] = 0;
    e->objects[idx].position[1] = 0;
    e->objects[idx].position[2] = 0;
    e->objects[idx].rotation[0] = 0;
    e->objects[idx].rotation[1] = 0;
    e->objects[idx].rotation[2] = 0;

    // Update gizmo if selected
    if (e->selectedObject == idx) {
        e->selectedPos[0] = 0; e->selectedPos[1] = 0; e->selectedPos[2] = 0;
    }
    e->dirty = true;
}

inline int get_object_count() {
    auto* e = get_engine();
    return e ? (int)e->objects.size() : 0;
}

inline const char* get_object_type(int idx) {
    auto* e = get_engine();
    if (e && idx >= 0 && idx < (int)e->objects.size()) {
        // Map type IDs to names (matching shader MAT_* defines)
        static const char* typeNames[] = {
            "Ground",    // 0 - MAT_GROUND
            "Blob",      // 1 - MAT_BLOB
            "Torus",     // 2 - MAT_TORUS
            "Carved",    // 3 - MAT_CARVED
            "Column",    // 4 - MAT_COLUMN
            "Repeated"   // 5 - MAT_REPEATED
        };
        int type = e->objects[idx].type;
        if (type >= 0 && type < 6) {
            return typeNames[type];
        }
        return "Object";
    }
    return "Unknown";
}

// Combined drag UI for object position - returns true if changed
inline bool imgui_drag_object_position(int idx, float speed) {
    auto* e = get_engine();
    if (!e || idx < 0 || idx >= (int)e->objects.size()) return false;

    float* pos = e->objects[idx].position;
    if (ImGui::DragFloat3("Position", pos, speed)) {
        // Also update selectedPos if this is the selected object
        if (e->selectedObject == idx) {
            e->selectedPos[0] = pos[0];
            e->selectedPos[1] = pos[1];
            e->selectedPos[2] = pos[2];
        }
        e->dirty = true;
        return true;
    }
    return false;
}

// Combined drag UI for object rotation - returns true if changed
inline bool imgui_drag_object_rotation(int idx, float speed) {
    auto* e = get_engine();
    if (!e || idx < 0 || idx >= (int)e->objects.size()) return false;

    float* rot = e->objects[idx].rotation;
    if (ImGui::DragFloat3("Rotation", rot, speed)) {
        // Also update selectedRot if this is the selected object
        if (e->selectedObject == idx) {
            e->selectedRot[0] = rot[0];
            e->selectedRot[1] = rot[1];
            e->selectedRot[2] = rot[2];
        }
        e->dirty = true;
        return true;
    }
    return false;
}

// ============================================================================
// Event Recording/Replay API for Automated Testing
// ============================================================================

// Start recording events
inline void start_recording() {
    if (!g_recordedEvents) {
        g_recordedEvents = new std::vector<RecordedEvent>();
    }
    g_recordedEvents->clear();
    g_recording = true;
    g_recordStartTime = SDL_GetTicks();
    std::cout << "Event recording started" << std::endl;
}

// Stop recording and return event count
inline size_t stop_recording() {
    g_recording = false;
    size_t count = g_recordedEvents ? g_recordedEvents->size() : 0;
    std::cout << "Event recording stopped. " << count << " events recorded." << std::endl;
    return count;
}

// Check if recording
inline bool is_recording() {
    return g_recording;
}

// Check if replaying
inline bool is_replaying() {
    return g_replaying;
}

// Get recorded events count
inline size_t get_recorded_event_count() {
    return g_recordedEvents ? g_recordedEvents->size() : 0;
}

// Save recorded events to a binary file
inline bool save_recorded_events(const char* filepath) {
    if (!g_recordedEvents || g_recordedEvents->empty()) {
        std::cerr << "No events to save" << std::endl;
        return false;
    }

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << filepath << std::endl;
        return false;
    }

    // Write header: magic number and event count
    uint32_t magic = 0x53444633; // "SDF3"
    uint32_t count = static_cast<uint32_t>(g_recordedEvents->size());
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write events
    for (const auto& evt : *g_recordedEvents) {
        file.write(reinterpret_cast<const char*>(&evt.timestamp), sizeof(evt.timestamp));
        file.write(reinterpret_cast<const char*>(&evt.event), sizeof(evt.event));
    }

    file.close();
    std::cout << "Saved " << count << " events to " << filepath << std::endl;
    return true;
}

// Load recorded events from a binary file
inline bool load_recorded_events(const char* filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << filepath << std::endl;
        return false;
    }

    // Read and verify header
    uint32_t magic, count;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));

    if (magic != 0x53444633) {
        std::cerr << "Invalid recording file format" << std::endl;
        return false;
    }

    // Initialize events vector if needed
    if (!g_recordedEvents) {
        g_recordedEvents = new std::vector<RecordedEvent>();
    }
    g_recordedEvents->clear();
    g_recordedEvents->reserve(count);

    // Read events
    for (uint32_t i = 0; i < count; i++) {
        RecordedEvent evt;
        file.read(reinterpret_cast<char*>(&evt.timestamp), sizeof(evt.timestamp));
        file.read(reinterpret_cast<char*>(&evt.event), sizeof(evt.event));
        g_recordedEvents->push_back(evt);
    }

    file.close();
    std::cout << "Loaded " << count << " events from " << filepath << std::endl;
    return true;
}

// Start replaying recorded events
inline void start_replay() {
    if (!g_recordedEvents || g_recordedEvents->empty()) {
        std::cerr << "No events to replay" << std::endl;
        return;
    }
    g_replaying = true;
    g_replayIndex = 0;
    g_replayStartTime = SDL_GetTicks();
    std::cout << "Event replay started" << std::endl;
}

// Stop replaying
inline void stop_replay() {
    g_replaying = false;
    std::cout << "Event replay stopped" << std::endl;
}

// Process replay events (call this in the event loop)
inline void process_replay_events() {
    if (!g_replaying || !g_recordedEvents) return;

    uint64_t elapsed = SDL_GetTicks() - g_replayStartTime;

    // Inject events whose timestamp has been reached
    while (g_replayIndex < g_recordedEvents->size()) {
        const auto& evt = (*g_recordedEvents)[g_replayIndex];
        if (evt.timestamp <= elapsed) {
            SDL_Event event = evt.event;
            SDL_PushEvent(&event);
            g_replayIndex++;
        } else {
            break;
        }
    }

    // Stop replay when all events have been processed
    if (g_replayIndex >= g_recordedEvents->size()) {
        stop_replay();
    }
}

// Inject a single event (for programmatic testing)
inline void push_event(const SDL_Event& event) {
    SDL_Event e = event;
    SDL_PushEvent(&e);
}

// Create and inject a mouse click event
inline void inject_mouse_click(float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event down, up;

    // Mouse button down
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = button;
    down.button.x = x;
    down.button.y = y;
    down.button.clicks = 1;
    SDL_PushEvent(&down);

    // Mouse button up
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.button = button;
    up.button.x = x;
    up.button.y = y;
    up.button.clicks = 1;
    SDL_PushEvent(&up);
}

// Create and inject a mouse motion event
inline void inject_mouse_motion(float x, float y, float xrel = 0, float yrel = 0) {
    SDL_Event motion;
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    motion.motion.xrel = xrel;
    motion.motion.yrel = yrel;
    SDL_PushEvent(&motion);
}

// Create and inject a key press event
inline void inject_key_press(SDL_Keycode key) {
    SDL_Event down, up;

    // Key down
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.key = key;
    down.key.repeat = false;
    SDL_PushEvent(&down);

    // Key up
    up.type = SDL_EVENT_KEY_UP;
    up.key.key = key;
    up.key.repeat = false;
    SDL_PushEvent(&up);
}

// Create and inject a scroll event
inline void inject_scroll(float dx, float dy) {
    SDL_Event scroll;
    scroll.type = SDL_EVENT_MOUSE_WHEEL;
    scroll.wheel.x = dx;
    scroll.wheel.y = dy;
    SDL_PushEvent(&scroll);
}

// ============================================================================
// Screenshot Capture
// ============================================================================

// Save the current compute image to a PNG file (downsized for smaller file)
inline bool save_screenshot(const char* filepath) {
    auto* e = get_engine();
    if (!e || !e->device || !e->computeImage) {
        std::cerr << "Engine not initialized" << std::endl;
        return false;
    }

    // ALWAYS dispatch compute shader to capture current pipeline state
    // (don't rely on dirty flag since main loop may have cleared it)
    {
        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = e->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(e->device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Transition to general for compute
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

        // Dispatch compute shader
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->computePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->computePipelineLayout,
                               0, 1, &e->descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, (e->swapchainExtent.width + 15) / 16, (e->swapchainExtent.height + 15) / 16, 1);

        // Transition to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(e->graphicsQueue);
        vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmd);

        std::cout << "[DEBUG] save_screenshot: dispatched compute shader" << std::endl;
    }

    uint32_t width = e->swapchainExtent.width;
    uint32_t height = e->swapchainExtent.height;
    VkDeviceSize imageSize = width * height * 4; // RGBA

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create staging buffer" << std::endl;
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(e->device, stagingBuffer, nullptr);
        std::cerr << "Failed to allocate staging memory" << std::endl;
        return false;
    }

    vkBindBufferMemory(e->device, stagingBuffer, stagingMemory, 0);

    // Create command buffer for copy
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

    // Transition image to transfer src
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = e->computeImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cmdBuffer, e->computeImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition image back
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(e->graphicsQueue);

    vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmdBuffer);

    // Map buffer
    void* data;
    vkMapMemory(e->device, stagingMemory, 0, imageSize, 0, &data);
    const uint8_t* pixels = static_cast<const uint8_t*>(data);

    // Downsample by 4x for smaller PNG (e.g., 2560x1440 -> 640x360)
    uint32_t scale = 4;
    uint32_t outWidth = width / scale;
    uint32_t outHeight = height / scale;

    // Allocate RGB buffer for downsampled image
    std::vector<uint8_t> rgbData(outWidth * outHeight * 3);

    for (uint32_t y = 0; y < outHeight; y++) {
        for (uint32_t x = 0; x < outWidth; x++) {
            // Simple point sampling (take center pixel of each block)
            // Flip Y to correct upside-down image
            uint32_t srcX = x * scale + scale / 2;
            uint32_t srcY = (outHeight - 1 - y) * scale + scale / 2;
            size_t srcIdx = (srcY * width + srcX) * 4;
            size_t dstIdx = (y * outWidth + x) * 3;

            rgbData[dstIdx + 0] = pixels[srcIdx + 0];  // R
            rgbData[dstIdx + 1] = pixels[srcIdx + 1];  // G
            rgbData[dstIdx + 2] = pixels[srcIdx + 2];  // B
        }
    }

    vkUnmapMemory(e->device, stagingMemory);
    vkDestroyBuffer(e->device, stagingBuffer, nullptr);
    vkFreeMemory(e->device, stagingMemory, nullptr);

    // Write PNG using stb_image_write
    int result = stbi_write_png(filepath, outWidth, outHeight, 3, rgbData.data(), outWidth * 3);

    if (result) {
        std::cout << "Screenshot saved to " << filepath << " (" << outWidth << "x" << outHeight << ")" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to write PNG: " << filepath << std::endl;
        return false;
    }
}

// ============================================================================
// SDF SAMPLING FOR MESH EXPORT
// ============================================================================
// This module provides GPU-accelerated SDF sampling for use with marching cubes.
// It extracts the sceneSDF function from the current shader and creates a
// specialized compute shader that samples the SDF at arbitrary points.

struct SDFSampler {
    bool initialized = false;
    VkBuffer inputBuffer = VK_NULL_HANDLE;
    VkDeviceMemory inputMemory = VK_NULL_HANDLE;
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
    // Find the sceneSDF function - it's the main SDF function
    // We need everything from primitives and operations up to sceneSDF

    std::string result;

    // Find where sceneSDF is defined
    size_t sceneSdfPos = shaderSource.find("float sceneSDF(vec3 p)");
    if (sceneSdfPos == std::string::npos) {
        // Try alternative form
        sceneSdfPos = shaderSource.find("float sceneSDF(");
    }

    if (sceneSdfPos == std::string::npos) {
        std::cerr << "Could not find sceneSDF function in shader" << std::endl;
        return "";
    }

    // Find the end of sceneSDF function (matching braces)
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

    // Extract sceneSDF
    std::string sceneSdf = shaderSource.substr(sceneSdfPos, funcEnd - sceneSdfPos);

    // Now find all helper functions that sceneSDF might call
    // Look for function definitions starting from SDF PRIMITIVES (not BOOLEAN OPERATIONS)
    // This ensures we capture primitive functions like sdCapsule that are needed by the scene
    size_t helperStart = shaderSource.find("// SDF PRIMITIVES");
    if (helperStart == std::string::npos) {
        // Fallback to BOOLEAN OPERATIONS if primitives marker not found
        helperStart = shaderSource.find("// BOOLEAN OPERATIONS");
    }
    if (helperStart == std::string::npos) {
        helperStart = shaderSource.find("float opUnion");
    }
    if (helperStart == std::string::npos) {
        helperStart = 0;
    }

    // Extract helper functions (between primitives/operations and sceneSDF)
    // This includes custom functions like sdHand, sdFinger, sdCapsule, etc.
    std::string helpers = shaderSource.substr(helperStart, sceneSdfPos - helperStart);

    return helpers + "\n" + sceneSdf;
}

// Build the sampler shader from template and current scene
inline std::string build_sampler_shader(const std::string& sceneShaderPath) {
    // Read the template
    std::string templatePath = sceneShaderPath.substr(0, sceneShaderPath.rfind('/')) + "/sdf_sampler.comp";
    std::string templateSrc = read_text_file(templatePath);
    if (templateSrc.empty()) {
        std::cerr << "Could not read sampler template: " << templatePath << std::endl;
        return "";
    }

    // Read the scene shader
    std::string sceneSrc = read_text_file(sceneShaderPath);
    if (sceneSrc.empty()) {
        std::cerr << "Could not read scene shader: " << sceneShaderPath << std::endl;
        return "";
    }

    // Extract sceneSDF and helpers
    std::string sceneCode = extract_scene_sdf(sceneSrc);
    if (sceneCode.empty()) {
        std::cerr << "Could not extract sceneSDF from shader" << std::endl;
        return "";
    }

    // Replace the marker in template with actual sceneSDF
    size_t markerStart = templateSrc.find("// MARKER_SCENE_SDF_START");
    size_t markerEnd = templateSrc.find("// MARKER_SCENE_SDF_END");
    if (markerStart == std::string::npos || markerEnd == std::string::npos) {
        std::cerr << "Could not find markers in sampler template" << std::endl;
        return "";
    }

    // Find the end of the MARKER_SCENE_SDF_END line
    markerEnd = templateSrc.find('\n', markerEnd) + 1;

    std::string result = templateSrc.substr(0, markerStart) +
                         "\n" + sceneCode + "\n" +
                         templateSrc.substr(markerEnd);

    return result;
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

    if (s->inputBuffer) vkDestroyBuffer(e->device, s->inputBuffer, nullptr);
    if (s->inputMemory) vkFreeMemory(e->device, s->inputMemory, nullptr);
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

    // Check if we need to reinitialize
    if (s->initialized && s->maxPoints >= numPoints && s->cachedShaderName == e->currentShaderName) {
        return true;  // Already initialized with enough capacity
    }

    // Cleanup existing resources
    if (s->initialized) {
        cleanup_sampler();
    }

    std::cout << "Initializing SDF sampler for " << numPoints << " points..." << std::endl;

    // Build the sampler shader
    std::string shaderPath = e->shaderDir + "/" + e->currentShaderName + ".comp";
    std::string samplerSrc = build_sampler_shader(shaderPath);
    if (samplerSrc.empty()) {
        return false;
    }

    // Compile shader
    auto spirv = compile_glsl_to_spirv(samplerSrc, "sdf_sampler.comp", shaderc_compute_shader);
    if (spirv.empty()) {
        std::cerr << "Failed to compile sampler shader" << std::endl;
        return false;
    }

    // Create shader module
    VkShaderModuleCreateInfo shaderModuleInfo{};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    shaderModuleInfo.pCode = spirv.data();

    if (vkCreateShaderModule(e->device, &shaderModuleInfo, nullptr, &s->shaderModule) != VK_SUCCESS) {
        std::cerr << "Failed to create sampler shader module" << std::endl;
        return false;
    }

    // Create buffers
    VkDeviceSize inputSize = numPoints * sizeof(float) * 4;  // vec4 per point
    VkDeviceSize outputSize = numPoints * sizeof(float);     // 1 float per point
    VkDeviceSize paramsSize = 16;  // uniform params (numPoints + padding)

    // Input buffer (staging, host visible)
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = inputSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &s->inputBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create input buffer" << std::endl;
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(e->device, s->inputBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(e, memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &s->inputMemory) != VK_SUCCESS) {
        std::cerr << "Failed to allocate input memory" << std::endl;
        return false;
    }
    vkBindBufferMemory(e->device, s->inputBuffer, s->inputMemory, 0);

    // Output buffer
    bufferInfo.size = outputSize;
    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &s->outputBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create output buffer" << std::endl;
        return false;
    }

    vkGetBufferMemoryRequirements(e->device, s->outputBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &s->outputMemory) != VK_SUCCESS) {
        std::cerr << "Failed to allocate output memory" << std::endl;
        return false;
    }
    vkBindBufferMemory(e->device, s->outputBuffer, s->outputMemory, 0);

    // Params buffer (uniform)
    bufferInfo.size = paramsSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (vkCreateBuffer(e->device, &bufferInfo, nullptr, &s->paramsBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to create params buffer" << std::endl;
        return false;
    }

    vkGetBufferMemoryRequirements(e->device, s->paramsBuffer, &memReq);
    allocInfo.allocationSize = memReq.size;

    if (vkAllocateMemory(e->device, &allocInfo, nullptr, &s->paramsMemory) != VK_SUCCESS) {
        std::cerr << "Failed to allocate params memory" << std::endl;
        return false;
    }
    vkBindBufferMemory(e->device, s->paramsBuffer, s->paramsMemory, 0);

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding bindings[3] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(e->device, &layoutInfo, nullptr, &s->descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout" << std::endl;
        return false;
    }

    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(e->device, &poolInfo, nullptr, &s->descriptorPool) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool" << std::endl;
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo dsAllocInfo{};
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = s->descriptorPool;
    dsAllocInfo.descriptorSetCount = 1;
    dsAllocInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkAllocateDescriptorSets(e->device, &dsAllocInfo, &s->descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor set" << std::endl;
        return false;
    }

    // Update descriptor set
    VkDescriptorBufferInfo inputBufferInfo{s->inputBuffer, 0, inputSize};
    VkDescriptorBufferInfo outputBufferInfo{s->outputBuffer, 0, outputSize};
    VkDescriptorBufferInfo paramsBufferInfo{s->paramsBuffer, 0, paramsSize};

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = s->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &inputBufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = s->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &outputBufferInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = s->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &paramsBufferInfo;

    vkUpdateDescriptorSets(e->device, 3, writes, 0, nullptr);

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &s->descriptorSetLayout;

    if (vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &s->pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        return false;
    }

    // Create compute pipeline
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
        std::cerr << "Failed to create compute pipeline" << std::endl;
        return false;
    }

    s->maxPoints = numPoints;
    s->cachedShaderName = e->currentShaderName;
    s->initialized = true;

    std::cout << "SDF sampler initialized successfully" << std::endl;
    return true;
}

// Sample SDF at given points, return distances
// positions: array of [x, y, z, 0] for each point
// Returns: array of float distances
inline std::vector<float> sample_sdf(const std::vector<float>& positions) {
    auto* s = get_sampler();
    auto* e = get_engine();

    size_t numPoints = positions.size() / 4;
    if (numPoints == 0) return {};

    // Initialize or reinitialize sampler if needed
    if (!init_sampler(numPoints)) {
        std::cerr << "Failed to initialize sampler" << std::endl;
        return {};
    }

    // Upload positions
    void* data;
    vkMapMemory(e->device, s->inputMemory, 0, positions.size() * sizeof(float), 0, &data);
    memcpy(data, positions.data(), positions.size() * sizeof(float));
    vkUnmapMemory(e->device, s->inputMemory);

    // Upload params
    struct { uint32_t numPoints; float pad1, pad2, pad3; } params = {
        static_cast<uint32_t>(numPoints), 0, 0, 0
    };
    vkMapMemory(e->device, s->paramsMemory, 0, sizeof(params), 0, &data);
    memcpy(data, &params, sizeof(params));
    vkUnmapMemory(e->device, s->paramsMemory);

    // Create command buffer
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

    // Dispatch compute shader
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s->pipeline);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s->pipelineLayout, 0, 1, &s->descriptorSet, 0, nullptr);

    // Workgroup size is 64, so dispatch enough groups
    uint32_t groupCount = (static_cast<uint32_t>(numPoints) + 63) / 64;
    vkCmdDispatch(cmdBuffer, groupCount, 1, 1);

    // Memory barrier to ensure compute shader writes are visible
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(e->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(e->graphicsQueue);

    vkFreeCommandBuffers(e->device, e->commandPool, 1, &cmdBuffer);

    // Read back results
    std::vector<float> results(numPoints);
    vkMapMemory(e->device, s->outputMemory, 0, numPoints * sizeof(float), 0, &data);
    memcpy(results.data(), data, numPoints * sizeof(float));
    vkUnmapMemory(e->device, s->outputMemory);

    return results;
}

// Convenience function to sample a single point
inline float sample_sdf_point(float x, float y, float z) {
    std::vector<float> pos = {x, y, z, 0.0f};
    auto results = sample_sdf(pos);
    return results.empty() ? 1e10f : results[0];
}

// Sample SDF on a regular 3D grid for marching cubes
// Returns flat array of distances in z-major order: [z0y0x0, z0y0x1, ..., z0y1x0, ...]
inline std::vector<float> sample_sdf_grid(
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    int resX, int resY, int resZ) {

    // Generate grid points
    size_t totalPoints = static_cast<size_t>(resX) * resY * resZ;
    std::vector<float> positions(totalPoints * 4);

    float stepX = (maxX - minX) / (resX - 1);
    float stepY = (maxY - minY) / (resY - 1);
    float stepZ = (maxZ - minZ) / (resZ - 1);

    size_t idx = 0;
    for (int iz = 0; iz < resZ; iz++) {
        for (int iy = 0; iy < resY; iy++) {
            for (int ix = 0; ix < resX; ix++) {
                positions[idx++] = minX + ix * stepX;
                positions[idx++] = minY + iy * stepY;
                positions[idx++] = minZ + iz * stepZ;
                positions[idx++] = 0.0f;  // padding
            }
        }
    }

    std::cout << "Sampling " << totalPoints << " SDF points on GPU..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    auto results = sample_sdf(positions);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "SDF sampling completed in " << duration.count() << " ms" << std::endl;

    return results;
}

// File-based SDF grid sampling for Jank interop
// Writes results to a binary file that can be read by Jank
inline bool sample_sdf_grid_to_file(
    const char* filename,
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    int resX, int resY, int resZ) {

    // Generate grid points
    size_t totalPoints = static_cast<size_t>(resX) * resY * resZ;
    std::vector<float> positions(totalPoints * 4);

    float stepX = (maxX - minX) / (resX - 1);
    float stepY = (maxY - minY) / (resY - 1);
    float stepZ = (maxZ - minZ) / (resZ - 1);

    size_t idx = 0;
    for (int iz = 0; iz < resZ; iz++) {
        for (int iy = 0; iy < resY; iy++) {
            for (int ix = 0; ix < resX; ix++) {
                positions[idx++] = minX + ix * stepX;
                positions[idx++] = minY + iy * stepY;
                positions[idx++] = minZ + iz * stepZ;
                positions[idx++] = 0.0f;  // padding
            }
        }
    }

    std::cout << "Sampling " << totalPoints << " SDF points on GPU to file..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    auto results = sample_sdf(positions);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "SDF sampling completed in " << duration.count() << " ms" << std::endl;

    // Write binary file: header (3 ints for resolution) + float data
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }

    // Write header: resX, resY, resZ
    file.write(reinterpret_cast<const char*>(&resX), sizeof(int));
    file.write(reinterpret_cast<const char*>(&resY), sizeof(int));
    file.write(reinterpret_cast<const char*>(&resZ), sizeof(int));

    // Write SDF values
    file.write(reinterpret_cast<const char*>(results.data()), results.size() * sizeof(float));
    file.close();

    std::cout << "SDF grid saved to " << filename << " (" << results.size() << " values)" << std::endl;
    return true;
}

// ============================================================================
// In-Memory SDF Grid Storage for Jank Access
// ============================================================================
// These globals keep the sampled grid in memory so Jank can access it without
// file-based transfer (avoids JIT compilation unit isolation issues)
// Uses env var to share storage address between viewer and JIT-compiled code

struct SDFGridStorage {
    std::vector<float>* data = nullptr;
    int resX = 0, resY = 0, resZ = 0;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
};

// Get the shared storage via environment variable (allows JIT code to access viewer's storage)
inline SDFGridStorage* create_sdf_grid_storage() {
    return new SDFGridStorage();
}

// Sample SDF grid and keep in memory (no file I/O)
inline int64_t sample_sdf_grid_to_memory(
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ,
    int resX, int resY, int resZ) {

    // Create new storage (handle-based API avoids JIT isolation issues)
    SDFGridStorage* storage = new SDFGridStorage();

    // Store bounds
    storage->minX = minX; storage->minY = minY; storage->minZ = minZ;
    storage->maxX = maxX; storage->maxY = maxY; storage->maxZ = maxZ;
    storage->resX = resX; storage->resY = resY; storage->resZ = resZ;

    // Generate grid points
    size_t totalPoints = static_cast<size_t>(resX) * resY * resZ;
    std::vector<float> positions(totalPoints * 4);

    float stepX = (maxX - minX) / (resX - 1);
    float stepY = (maxY - minY) / (resY - 1);
    float stepZ = (maxZ - minZ) / (resZ - 1);

    size_t idx = 0;
    for (int iz = 0; iz < resZ; iz++) {
        for (int iy = 0; iy < resY; iy++) {
            for (int ix = 0; ix < resX; ix++) {
                positions[idx++] = minX + ix * stepX;
                positions[idx++] = minY + iy * stepY;
                positions[idx++] = minZ + iz * stepZ;
                positions[idx++] = 0.0f;
            }
        }
    }

    std::cout << "Sampling " << totalPoints << " SDF points on GPU to memory..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    auto results = sample_sdf(positions);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "SDF sampling completed in " << duration.count() << " ms" << std::endl;

    // Store results
    storage->data = new std::vector<float>(std::move(results));

    std::cout << "SDF grid stored in memory (" << storage->data->size() << " values)" << std::endl;
    std::cout << "Returning handle: " << reinterpret_cast<int64_t>(storage) << std::endl;
    
    // Return pointer as handle for use by accessor functions
    return reinterpret_cast<int64_t>(storage);
}

// Accessor functions for Jank (handle-based API to avoid JIT isolation issues)
// All functions take `handle` (int64_t) as first parameter - this is the value returned by sample_sdf_grid_to_memory

inline int64_t sdf_grid_res_x(int64_t handle) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    return s ? s->resX : 0;
}
inline int64_t sdf_grid_res_y(int64_t handle) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    return s ? s->resY : 0;
}
inline int64_t sdf_grid_res_z(int64_t handle) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    return s ? s->resZ : 0;
}
inline int64_t sdf_grid_total_points(int64_t handle) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    if (!s) return 0;
    return static_cast<int64_t>(s->resX) * s->resY * s->resZ;
}

inline double sdf_grid_value_at(int64_t handle, int64_t index) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    if (!s || !s->data || index < 0 || index >= static_cast<int64_t>(s->data->size())) {
        return 999999.0;
    }
    return static_cast<double>((*s->data)[index]);
}

inline double sdf_grid_value_xyz(int64_t handle, int64_t ix, int64_t iy, int64_t iz) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    if (!s || !s->data) return 999999.0;
    if (ix < 0 || ix >= s->resX) return 999999.0;
    if (iy < 0 || iy >= s->resY) return 999999.0;
    if (iz < 0 || iz >= s->resZ) return 999999.0;
    int64_t idx = iz * s->resY * s->resX + iy * s->resX + ix;
    return static_cast<double>((*s->data)[idx]);
}

// Get bounds for marching cubes (handle-based)
inline double sdf_grid_min_x(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->minX : 0; }
inline double sdf_grid_min_y(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->minY : 0; }
inline double sdf_grid_min_z(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->minZ : 0; }
inline double sdf_grid_max_x(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->maxX : 0; }
inline double sdf_grid_max_y(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->maxY : 0; }
inline double sdf_grid_max_z(int64_t handle) { auto* s = reinterpret_cast<SDFGridStorage*>(handle); return s ? s->maxZ : 0; }

// Free the storage when done
inline void sdf_grid_free(int64_t handle) {
    auto* s = reinterpret_cast<SDFGridStorage*>(handle);
    if (s) {
        if (s->data) delete s->data;
        delete s;
    }
}

} // namespace sdf
