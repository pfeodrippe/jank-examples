// jolt_c.h - C API header for JoltPhysics wrapper
// Use with jank header requires: ["jolt_c.h" :as jolt :scope ""]

#ifndef JOLT_C_H
#define JOLT_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global initialization
void jolt_global_init(void);
void jolt_global_cleanup(void);

// World management
void* jolt_world_create(void);
void jolt_world_destroy(void* world_ptr);
void jolt_world_step(void* world_ptr, float delta_time, int collision_steps);
void jolt_world_optimize_broad_phase(void* world_ptr);
int jolt_world_get_num_bodies(void* world_ptr);
int jolt_world_get_num_active_bodies(void* world_ptr);

// Shape creation
void* jolt_shape_create_sphere(float radius);
void* jolt_shape_create_box(float half_x, float half_y, float half_z);
void* jolt_shape_create_capsule(float half_height, float radius);
void* jolt_shape_create_plane(float nx, float ny, float nz, float distance);
void jolt_shape_release(void* shape_ptr);

// Body creation
uint32_t jolt_body_create_with_shape(void* world_ptr, void* shape_ptr,
                                      float x, float y, float z,
                                      int is_dynamic, int activate);
uint32_t jolt_body_create_sphere(void* world_ptr, float x, float y, float z,
                                  float radius, int is_dynamic, int activate);
uint32_t jolt_body_create_box(void* world_ptr, float x, float y, float z,
                               float half_x, float half_y, float half_z,
                               int is_dynamic, int activate);

// Body manipulation
void jolt_body_set_velocity(void* world_ptr, uint32_t body_id, float vx, float vy, float vz);
void jolt_body_get_position(void* world_ptr, uint32_t body_id, float* out_x, float* out_y, float* out_z);
void jolt_body_get_velocity(void* world_ptr, uint32_t body_id, float* out_vx, float* out_vy, float* out_vz);
void jolt_body_destroy(void* world_ptr, uint32_t body_id);

#ifdef __cplusplus
}
#endif

#endif // JOLT_C_H
