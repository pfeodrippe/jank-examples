// =============================================================================
// vybe_flecs_jank.cpp - Flecs helpers that require jank runtime types
// This file is compiled separately and linked into standalone builds
// =============================================================================

#include "flecs.h"
#include "vybe_flecs_helpers.h"

#include <jank/runtime/core.hpp>
#include <jank/runtime/behavior/callable.hpp>
#include <jank/runtime/obj/persistent_vector.hpp>
#include <jank/runtime/obj/big_integer.hpp>

#include <unordered_map>

// =============================================================================
// System callback infrastructure
// =============================================================================

// Use lazy initialization (Meyer's Singleton) to ensure proper initialization
// when loaded as an object file via JIT
static std::unordered_map<ecs_entity_t, jank::runtime::object_ref>& get_system_callbacks() {
  static std::unordered_map<ecs_entity_t, jank::runtime::object_ref> callbacks;
  return callbacks;
}

extern "C" void vybe_register_system_callback(ecs_entity_t system_id, jank::runtime::object_ref callback) {
  get_system_callbacks()[system_id] = callback;
}

extern "C" void vybe_unregister_system_callback(ecs_entity_t system_id) {
  get_system_callbacks().erase(system_id);
}

// The C callback that Flecs will call - dispatches to jank
void vybe_system_dispatcher(ecs_iter_t* it) {
  ecs_entity_t system_id = it->system;
  auto& callbacks = get_system_callbacks();
  auto callback_it = callbacks.find(system_id);
  if (callback_it == callbacks.end()) {
    return;
  }
  jank::runtime::object_ref callback = callback_it->second;
  auto iter_int = jank::runtime::make_box<jank::runtime::obj::integer>(
    reinterpret_cast<int64_t>(it));
  auto world_int = jank::runtime::make_box<jank::runtime::obj::integer>(
    reinterpret_cast<int64_t>(it->world));
  auto args = jank::runtime::make_box<jank::runtime::obj::persistent_vector>(
    std::in_place, world_int, iter_int);
  jank::runtime::apply_to(callback, args);
}

// Delete a system and unregister its callback
void vybe_delete_system(ecs_world_t* w, ecs_entity_t e) {
  get_system_callbacks().erase(e);
  ecs_delete(w, e);
}

// Create a system with the dispatcher callback
extern "C" ecs_entity_t vybe_create_system(ecs_world_t* w, const char* name, const char* query_expr) {
  ecs_entity_t existing = ecs_lookup(w, name);
  if (existing != 0 && vybe_is_system(w, existing)) {
    vybe_delete_system(w, existing);
  }
  ecs_system_desc_t desc = {};
  ecs_entity_desc_t edesc = {};
  edesc.name = name;
  desc.entity = ecs_entity_init(w, &edesc);
  ecs_add_pair(w, desc.entity, EcsDependsOn, EcsOnUpdate);
  ecs_add_id(w, desc.entity, EcsOnUpdate);
  desc.query.expr = query_expr;
  desc.callback = vybe_system_dispatcher;
  return ecs_system_init(w, &desc);
}

// Create entity with a name (symbol) - uses jank::runtime::to_string
extern "C" ecs_entity_t vybe_create_entity_with_name(ecs_world_t* w, jank::runtime::object_ref name_obj) {
  auto name_str = jank::runtime::to_string(name_obj);
  ecs_entity_desc_t desc = {};
  desc.name = name_str.c_str();
  ecs_entity_t e = ecs_entity_init(w, &desc);
  if (e > 0) {
    ecs_set_symbol(w, e, name_str.c_str());
  }
  return e;
}

// =============================================================================
// Query helpers that return jank vectors
// =============================================================================

jank::runtime::object_ref vybe_query_entities(ecs_world_t* w, ecs_query_t* q) {
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_iter_t it = ecs_query_iter(w, q);
  while (ecs_query_next(&it)) {
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t entity = it.entities[i];
      result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(entity)));
    }
  }
  return result;
}

jank::runtime::object_ref vybe_query_entities_str(ecs_world_t* w, const char* query_str) {
  ecs_query_desc_t desc = {};
  desc.expr = query_str;
  ecs_query_t* q = ecs_query_init(w, &desc);
  if (!q) {
    return jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  }
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_iter_t it = ecs_query_iter(w, q);
  while (ecs_query_next(&it)) {
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t entity = it.entities[i];
      result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(entity)));
    }
  }
  ecs_query_fini(q);
  return result;
}

// =============================================================================
// Entity helpers that return jank vectors
// =============================================================================

jank::runtime::object_ref vybe_entity_ids(ecs_world_t* w, ecs_entity_t e) {
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  const ecs_type_t* type = ecs_get_type(w, e);
  if (!type) return result;
  for (int i = 0; i < type->count; i++) {
    ecs_entity_t id = type->array[i];
    result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(id)));
  }
  return result;
}

jank::runtime::object_ref vybe_all_named_entities(ecs_world_t* w) {
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_query_desc_t desc = {};
  desc.expr = "$this != 0";
  ecs_query_t* q = ecs_query_init(w, &desc);
  if (!q) return result;
  ecs_iter_t it = ecs_query_iter(w, q);
  while (ecs_query_next(&it)) {
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t e = it.entities[i];
      const char* sym = ecs_get_symbol(w, e);
      if (sym && sym[0] != '\0') {
        result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(e)));
      }
    }
  }
  ecs_query_fini(q);
  return result;
}

jank::runtime::object_ref vybe_children_ids(ecs_world_t* w, ecs_entity_t parent) {
  jank::runtime::object_ref result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_iter_t it = ecs_children(w, parent);
  while (ecs_children_next(&it)) {
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t child = it.entities[i];
      result = jank::runtime::conj(result, jank::runtime::make_box(static_cast<int64_t>(child)));
    }
  }
  return result;
}
