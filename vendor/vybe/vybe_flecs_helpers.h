#ifndef VYBE_FLECS_HELPERS_H
#define VYBE_FLECS_HELPERS_H

// vybe Flecs helpers - external header to avoid ODR violations in jank standalone builds
// These functions were originally in cpp/raw blocks in vybe/flecs.jank
// NOTE: Functions that use jank::runtime types remain in the .jank file's cpp/raw

#include "flecs.h"
#include <cstdint>

// =============================================================================
// System helpers (pure flecs, no jank runtime dependency)
// =============================================================================

// Check if an entity is a system
inline bool vybe_is_system(ecs_world_t* w, ecs_entity_t e) {
  return ecs_has_id(w, e, EcsSystem);
}

// Create a system with the dispatcher callback
// If a system with the same name exists, delete it first
// NOTE: Caller must set desc.callback externally (can't reference vybe_system_dispatcher here)
inline ecs_entity_t vybe_create_system_base(ecs_world_t* w, const char* name, const char* query_expr, ecs_iter_action_t callback) {
  // Check if entity with this name already exists
  ecs_entity_t existing = ecs_lookup(w, name);
  if (existing != 0 && vybe_is_system(w, existing)) {
    // Delete the existing system
    ecs_delete(w, existing);
  }

  ecs_system_desc_t desc = {};

  // Create entity for system with name and add to OnUpdate phase
  ecs_entity_desc_t edesc = {};
  edesc.name = name;
  desc.entity = ecs_entity_init(w, &edesc);

  // Add to OnUpdate phase
  ecs_add_pair(w, desc.entity, EcsDependsOn, EcsOnUpdate);
  ecs_add_id(w, desc.entity, EcsOnUpdate);

  // Set query expression
  desc.query.expr = query_expr;

  // Use provided callback
  desc.callback = callback;

  return ecs_system_init(w, &desc);
}

// =============================================================================
// World/Entity helpers (pure flecs)
// =============================================================================

// Check if entity is alive
inline bool vybe_entity_alive(ecs_world_t* w, ecs_entity_t e) {
  return ecs_is_alive(w, e);
}

// Get entity name (symbol)
inline const char* vybe_entity_name(ecs_world_t* w, ecs_entity_t e) {
  const char* name = ecs_get_symbol(w, e);
  return name ? name : "";
}

// Get entity type string (all components/tags)
inline const char* vybe_type_str(ecs_world_t* w, ecs_entity_t e) {
  const ecs_type_t* type = ecs_get_type(w, e);
  if (!type) return "";
  return ecs_type_str(w, type);
}

// Check if entity has a component/tag
inline bool vybe_has_id(ecs_world_t* w, ecs_entity_t e, ecs_entity_t id) {
  return ecs_has_id(w, e, id);
}

// Check if an ID is a tag (no data, just a marker)
inline bool vybe_is_tag(ecs_world_t* w, ecs_entity_t id) {
  const ecs_type_info_t* ti = ecs_get_type_info(w, id);
  return ti == nullptr || ti->size == 0;
}

// Remove component/tag from entity
inline void vybe_remove_id(ecs_world_t* w, ecs_entity_t e, ecs_entity_t id) {
  ecs_remove_id(w, e, id);
}

// Delete entity
inline void vybe_delete_entity(ecs_world_t* w, ecs_entity_t e) {
  ecs_delete(w, e);
}

// Convert a pointer to int64 for use as a stable cache key
inline int64_t vybe_ptr_to_int64(ecs_world_t* ptr) {
  return reinterpret_cast<int64_t>(ptr);
}

// =============================================================================
// Iterator helpers for system callbacks (pure flecs)
// =============================================================================

// Get the number of entities in the current iterator table
inline int32_t vybe_iter_count(ecs_iter_t* it) {
  return it->count;
}

// Get entity ID at index from iterator
inline ecs_entity_t vybe_iter_entity(ecs_iter_t* it, int32_t idx) {
  return it->entities[idx];
}

// Get field data pointer for a given field index and entity index
// field_index is 0-based (first component in query = 0)
inline void* vybe_iter_field_ptr(ecs_iter_t* it, int32_t field_index, size_t field_size, int32_t entity_idx) {
  void* base = ecs_field_w_size(it, field_size, field_index);
  if (!base) return nullptr;
  return static_cast<char*>(base) + (entity_idx * field_size);
}

// Helpers that take integer (pointer address) instead of pointer
// This allows passing pointers from jank as integers
inline int32_t vybe_iter_count_int(int64_t it_addr) {
  ecs_iter_t* it = reinterpret_cast<ecs_iter_t*>(it_addr);
  return it->count;
}

inline ecs_entity_t vybe_iter_entity_int(int64_t it_addr, int32_t idx) {
  ecs_iter_t* it = reinterpret_cast<ecs_iter_t*>(it_addr);
  return it->entities[idx];
}

// Create a pair from two entity IDs
inline ecs_entity_t vybe_ecs_pair(ecs_entity_t first, ecs_entity_t second) {
  return ecs_pair(first, second);
}

// Get EcsChildOf constant
inline ecs_entity_t vybe_EcsChildOf() {
  return EcsChildOf;
}

// =============================================================================
// Entity descriptor helper
// =============================================================================

// Set entity name on a descriptor (workaround for jank cpp/value limitation)
inline void set_entity_name(ecs_entity_desc_t* desc, const char* name) {
  desc->name = name;
}

#endif // VYBE_FLECS_HELPERS_H
