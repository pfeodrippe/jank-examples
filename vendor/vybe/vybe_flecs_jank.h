// =============================================================================
// vybe_flecs_jank.h - Forward declarations for jank-runtime-dependent functions
// Implementation is in vybe_flecs_jank.cpp (compiled to vybe_flecs_jank.o)
// =============================================================================

#ifndef VYBE_FLECS_JANK_H
#define VYBE_FLECS_JANK_H

#include "flecs.h"

// Forward declare jank runtime types
namespace jank::runtime {
  template<typename T> class oref;
  struct object;
  using object_ref = oref<object>;
}

// System callback infrastructure
extern "C" void vybe_register_system_callback(ecs_entity_t system_id, jank::runtime::object_ref callback);
extern "C" void vybe_unregister_system_callback(ecs_entity_t system_id);
extern "C" ecs_entity_t vybe_create_system(ecs_world_t* w, const char* name, const char* query_expr);
extern "C" ecs_entity_t vybe_create_entity_with_name(ecs_world_t* w, jank::runtime::object_ref name_obj);

// Query helpers that return jank vectors
jank::runtime::object_ref vybe_query_entities(ecs_world_t* w, ecs_query_t* q);
jank::runtime::object_ref vybe_query_entities_str(ecs_world_t* w, const char* query_str);

// Entity helpers that return jank vectors
jank::runtime::object_ref vybe_entity_ids(ecs_world_t* w, ecs_entity_t e);
jank::runtime::object_ref vybe_all_named_entities(ecs_world_t* w);
jank::runtime::object_ref vybe_children_ids(ecs_world_t* w, ecs_entity_t parent);

#endif // VYBE_FLECS_JANK_H
