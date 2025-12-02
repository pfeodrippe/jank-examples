// jolt_wrapper.cpp - Precompiled wrapper for JoltPhysics
// This file is compiled alongside Jolt's object files, so vtables are consistent.
// JIT code can safely call these functions without vtable mismatch issues.

#include <cstdio>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>

JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace JPH::literals;

// Layer definitions
namespace JoltLayers
{
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}

namespace JoltBroadPhaseLayers
{
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS(2);
}

// World state structure
struct JoltWorld
{
    TempAllocator* temp_allocator = nullptr;  // Use base class pointer
    JobSystemSingleThreaded* job_system = nullptr;
    BroadPhaseLayerInterfaceTable* broad_phase_layer_interface = nullptr;
    ObjectLayerPairFilterTable* object_vs_object_layer_filter = nullptr;
    ObjectVsBroadPhaseLayerFilterTable* object_vs_broadphase_filter = nullptr;
    PhysicsSystem* physics_system = nullptr;
    bool owns_temp_allocator = false;
};

static bool g_jolt_initialized = false;

// =============================================================================
// Extern "C" wrapper functions - callable from JIT code
// =============================================================================

extern "C" {

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

void jolt_global_init()
{
    if (g_jolt_initialized) return;

    printf("[jolt] Initializing Jolt...\n");

    printf("[jolt] RegisterDefaultAllocator...\n");
    RegisterDefaultAllocator();

    printf("[jolt] Creating Factory...\n");
    if (Factory::sInstance == nullptr) {
        Factory::sInstance = new Factory();
    }

    printf("[jolt] RegisterTypes...\n");
    RegisterTypes();
    g_jolt_initialized = true;
    printf("[jolt] Initialization complete!\n");
}

void jolt_global_cleanup()
{
    if (!g_jolt_initialized) return;

    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    g_jolt_initialized = false;
}

// -----------------------------------------------------------------------------
// World management
// -----------------------------------------------------------------------------

void* jolt_world_create()
{
    printf("[jolt] Creating world...\n");
    jolt_global_init();

    auto* world = new JoltWorld();

    // Temp allocator - use TempAllocatorMalloc for WASM compatibility
    printf("[jolt] Creating TempAllocatorMalloc...\n");
#ifdef __EMSCRIPTEN__
    // For WASM, use malloc-based allocator (simpler, no large preallocation)
    world->temp_allocator = new TempAllocatorMalloc();
    world->owns_temp_allocator = true;
#else
    // For native, use the faster impl with preallocated buffer
    world->temp_allocator = new TempAllocatorImpl(10 * 1024 * 1024);
    world->owns_temp_allocator = true;
#endif

    // Job system
    printf("[jolt] Creating JobSystemSingleThreaded...\n");
    world->job_system = new JobSystemSingleThreaded(64);

    // Layer interfaces
    printf("[jolt] Creating layer interfaces...\n");
    world->broad_phase_layer_interface = new BroadPhaseLayerInterfaceTable(JoltLayers::NUM_LAYERS, JoltBroadPhaseLayers::NUM_LAYERS);
    world->broad_phase_layer_interface->MapObjectToBroadPhaseLayer(JoltLayers::NON_MOVING, JoltBroadPhaseLayers::NON_MOVING);
    world->broad_phase_layer_interface->MapObjectToBroadPhaseLayer(JoltLayers::MOVING, JoltBroadPhaseLayers::MOVING);

    world->object_vs_object_layer_filter = new ObjectLayerPairFilterTable(JoltLayers::NUM_LAYERS);
    world->object_vs_object_layer_filter->EnableCollision(JoltLayers::NON_MOVING, JoltLayers::MOVING);
    world->object_vs_object_layer_filter->EnableCollision(JoltLayers::MOVING, JoltLayers::MOVING);
    world->object_vs_object_layer_filter->EnableCollision(JoltLayers::MOVING, JoltLayers::NON_MOVING);

    world->object_vs_broadphase_filter = new ObjectVsBroadPhaseLayerFilterTable(
        *world->broad_phase_layer_interface,
        JoltBroadPhaseLayers::NUM_LAYERS,
        *world->object_vs_object_layer_filter,
        JoltLayers::NUM_LAYERS
    );

    // Physics system
    printf("[jolt] Creating PhysicsSystem...\n");
    const uint cMaxBodies = 1024;
    const uint cNumBodyMutexes = 0;
    const uint cMaxBodyPairs = 1024;
    const uint cMaxContactConstraints = 1024;

    world->physics_system = new PhysicsSystem();
    printf("[jolt] Initializing PhysicsSystem...\n");
    world->physics_system->Init(
        cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        *world->broad_phase_layer_interface,
        *world->object_vs_broadphase_filter,
        *world->object_vs_object_layer_filter
    );

    // Set gravity
    printf("[jolt] Setting gravity...\n");
    world->physics_system->SetGravity(Vec3(0.0f, -9.81f, 0.0f));

    printf("[jolt] World created successfully!\n");
    return static_cast<void*>(world);
}

void jolt_world_destroy(void* world_ptr)
{
    if (!world_ptr) return;

    auto* world = static_cast<JoltWorld*>(world_ptr);

    delete world->physics_system;
    delete world->object_vs_broadphase_filter;
    delete world->object_vs_object_layer_filter;
    delete world->broad_phase_layer_interface;
    delete world->job_system;
    delete world->temp_allocator;
    delete world;
}

// -----------------------------------------------------------------------------
// Shape creation - PRECOMPILED, vtables are correct!
// -----------------------------------------------------------------------------

void* jolt_shape_create_sphere(float radius)
{
    // Shape created in precompiled code - vtable is correct
    return new SphereShape(radius);
}

void* jolt_shape_create_box(float half_x, float half_y, float half_z)
{
    return new BoxShape(Vec3(half_x, half_y, half_z));
}

void* jolt_shape_create_capsule(float half_height, float radius)
{
    return new CapsuleShape(half_height, radius);
}

void* jolt_shape_create_plane(float nx, float ny, float nz, float distance)
{
    Plane plane(Vec3(nx, ny, nz).Normalized(), distance);
    return new PlaneShape(plane);
}

void jolt_shape_release(void* shape_ptr)
{
    if (!shape_ptr) return;
    auto* shape = static_cast<Shape*>(shape_ptr);
    // Shapes use reference counting
    shape->Release();
}

// -----------------------------------------------------------------------------
// Body creation - uses precompiled shapes with correct vtables
// -----------------------------------------------------------------------------

uint32_t jolt_body_create_with_shape(
    void* world_ptr,
    void* shape_ptr,
    float x, float y, float z,
    bool is_dynamic,
    bool activate)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    auto* shape = static_cast<Shape*>(shape_ptr);

    BodyInterface& body_interface = world->physics_system->GetBodyInterface();

    BodyCreationSettings settings(
        shape,
        RVec3(Real(x), Real(y), Real(z)),
        Quat::sIdentity(),
        is_dynamic ? EMotionType::Dynamic : EMotionType::Static,
        is_dynamic ? JoltLayers::MOVING : JoltLayers::NON_MOVING
    );

    BodyID body_id = body_interface.CreateAndAddBody(
        settings,
        activate ? EActivation::Activate : EActivation::DontActivate
    );

    return body_id.GetIndexAndSequenceNumber();
}

// Convenience functions that create shape + body in one call
uint32_t jolt_body_create_sphere(
    void* world_ptr,
    float x, float y, float z,
    float radius,
    bool is_dynamic,
    bool activate)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();

    // Shape created here in precompiled code - vtable correct!
    BodyCreationSettings settings(
        new SphereShape(radius),
        RVec3(Real(x), Real(y), Real(z)),
        Quat::sIdentity(),
        is_dynamic ? EMotionType::Dynamic : EMotionType::Static,
        is_dynamic ? JoltLayers::MOVING : JoltLayers::NON_MOVING
    );

    BodyID body_id = body_interface.CreateAndAddBody(
        settings,
        activate ? EActivation::Activate : EActivation::DontActivate
    );

    return body_id.GetIndexAndSequenceNumber();
}

uint32_t jolt_body_create_box(
    void* world_ptr,
    float x, float y, float z,
    float half_x, float half_y, float half_z,
    bool is_dynamic,
    bool activate)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();

    BodyCreationSettings settings(
        new BoxShape(Vec3(half_x, half_y, half_z)),
        RVec3(Real(x), Real(y), Real(z)),
        Quat::sIdentity(),
        is_dynamic ? EMotionType::Dynamic : EMotionType::Static,
        is_dynamic ? JoltLayers::MOVING : JoltLayers::NON_MOVING
    );

    BodyID body_id = body_interface.CreateAndAddBody(
        settings,
        activate ? EActivation::Activate : EActivation::DontActivate
    );

    return body_id.GetIndexAndSequenceNumber();
}

// -----------------------------------------------------------------------------
// Body manipulation
// -----------------------------------------------------------------------------

void jolt_body_set_velocity(void* world_ptr, uint32_t body_id_raw, float vx, float vy, float vz)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();
    BodyID body_id(body_id_raw);
    body_interface.SetLinearVelocity(body_id, Vec3(vx, vy, vz));
}

void jolt_body_get_position(void* world_ptr, uint32_t body_id_raw, float* out_x, float* out_y, float* out_z)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();
    BodyID body_id(body_id_raw);
    RVec3 pos = body_interface.GetCenterOfMassPosition(body_id);
    *out_x = static_cast<float>(pos.GetX());
    *out_y = static_cast<float>(pos.GetY());
    *out_z = static_cast<float>(pos.GetZ());
}

void jolt_body_get_velocity(void* world_ptr, uint32_t body_id_raw, float* out_vx, float* out_vy, float* out_vz)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();
    BodyID body_id(body_id_raw);
    Vec3 vel = body_interface.GetLinearVelocity(body_id);
    *out_vx = vel.GetX();
    *out_vy = vel.GetY();
    *out_vz = vel.GetZ();
}

bool jolt_body_is_active(void* world_ptr, uint32_t body_id_raw)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();
    BodyID body_id(body_id_raw);
    return body_interface.IsActive(body_id);
}

void jolt_body_destroy(void* world_ptr, uint32_t body_id_raw)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    BodyInterface& body_interface = world->physics_system->GetBodyInterface();
    BodyID body_id(body_id_raw);
    body_interface.RemoveBody(body_id);
    body_interface.DestroyBody(body_id);
}

// -----------------------------------------------------------------------------
// Simulation
// -----------------------------------------------------------------------------

void jolt_world_step(void* world_ptr, float delta_time, int collision_steps)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    world->physics_system->Update(
        delta_time,
        collision_steps,
        world->temp_allocator,
        world->job_system
    );
}

void jolt_world_optimize_broad_phase(void* world_ptr)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    world->physics_system->OptimizeBroadPhase();
}

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

int jolt_world_get_num_bodies(void* world_ptr)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    return static_cast<int>(world->physics_system->GetNumBodies());
}

int jolt_world_get_num_active_bodies(void* world_ptr)
{
    auto* world = static_cast<JoltWorld*>(world_ptr);
    return static_cast<int>(world->physics_system->GetNumActiveBodies(EBodyType::RigidBody));
}

} // extern "C"
