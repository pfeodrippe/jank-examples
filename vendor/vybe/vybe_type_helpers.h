#ifndef VYBE_TYPE_HELPERS_H
#define VYBE_TYPE_HELPERS_H

// vybe type helpers - external header to avoid ODR violations in jank standalone builds
// These functions were originally in cpp/raw blocks in vybe/type.jank

#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "flecs.h"

// =============================================================================
// Timing helper
// =============================================================================

inline void eita()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
    std::cout << "The system clock is currently at " << std::ctime(&t_c);
}

// =============================================================================
// Struct builder pattern
// =============================================================================

struct VybeStructBuilder {
  ecs_world_t* world;
  ecs_struct_desc_t desc;
  int member_idx;
};

inline VybeStructBuilder* vybe_struct_begin(ecs_world_t* w, const char* name) {
  auto* b = new VybeStructBuilder();
  b->world = w;
  b->desc = {};
  ecs_entity_desc_t ed = {}; ed.name = name;
  b->desc.entity = ecs_entity_init(w, &ed);
  b->member_idx = 0;
  return b;
}

inline void vybe_struct_add_member(VybeStructBuilder* b, const char* name, ecs_entity_t type) {
  if (b->member_idx < 32) {
    b->desc.members[b->member_idx].name = name;
    b->desc.members[b->member_idx].type = type;
    b->member_idx++;
  }
}

inline ecs_entity_t vybe_struct_end(VybeStructBuilder* b) {
  ecs_entity_t result = ecs_struct_init(b->world, &b->desc);
  delete b;
  return result;
}

// =============================================================================
// Field access helpers using meta cursor API
// =============================================================================

// Add component to entity (ensures it exists)
inline void vybe_add_comp(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp) {
  if (!w || !e || !comp) return;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (ti && ti->size > 0) {
    ecs_ensure_id(w, e, comp, ti->size);
  }
}

// Set a float field on a component (entity version - gets pointer internally)
inline void vybe_set_field_float(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name, double value) {
  if (!w || !e || !comp || !field_name) return;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return;
  void* ptr = ecs_ensure_id(w, e, comp, ti->size);
  if (!ptr) return;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, ptr);
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  ecs_meta_set_float(&cur, value);
}

// Set an int field on a component
inline void vybe_set_field_int(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name, int64_t value) {
  if (!w || !e || !comp || !field_name) return;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return;
  void* ptr = ecs_ensure_id(w, e, comp, ti->size);
  if (!ptr) return;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, ptr);
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  ecs_meta_set_int(&cur, value);
}

// Set a uint field on a component
inline void vybe_set_field_uint(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name, uint64_t value) {
  if (!w || !e || !comp || !field_name) return;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return;
  void* ptr = ecs_ensure_id(w, e, comp, ti->size);
  if (!ptr) return;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, ptr);
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  ecs_meta_set_uint(&cur, value);
}

// Set a bool field on a component
inline void vybe_set_field_bool(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name, bool value) {
  if (!w || !e || !comp || !field_name) return;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return;
  void* ptr = ecs_ensure_id(w, e, comp, ti->size);
  if (!ptr) return;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, ptr);
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  ecs_meta_set_bool(&cur, value);
}

// Check if a component is registered in the given world
inline bool vybe_has_type_info(ecs_world_t* w, ecs_entity_t comp) {
  if (!w || !comp) return false;
  return ecs_get_type_info(w, comp) != nullptr;
}

// Get a float field from a component
inline double vybe_get_field_float(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name) {
  if (!w || !e || !comp || !field_name) return 0.0;
  const void* ptr = ecs_get_id(w, e, comp);
  if (!ptr) return 0.0;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, const_cast<void*>(ptr));
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  return ecs_meta_get_float(&cur);
}

// Get an int field from a component
inline int64_t vybe_get_field_int(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name) {
  if (!w || !e || !comp || !field_name) return 0;
  const void* ptr = ecs_get_id(w, e, comp);
  if (!ptr) return 0;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, const_cast<void*>(ptr));
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  return ecs_meta_get_int(&cur);
}

// Get a uint field from a component
inline uint64_t vybe_get_field_uint(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name) {
  if (!w || !e || !comp || !field_name) return 0;
  const void* ptr = ecs_get_id(w, e, comp);
  if (!ptr) return 0;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, const_cast<void*>(ptr));
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  return ecs_meta_get_uint(&cur);
}

// Get a bool field from a component
inline bool vybe_get_field_bool(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp, const char* field_name) {
  if (!w || !e || !comp || !field_name) return false;
  const void* ptr = ecs_get_id(w, e, comp);
  if (!ptr) return false;
  ecs_meta_cursor_t cur = ecs_meta_cursor(w, comp, const_cast<void*>(ptr));
  ecs_meta_push(&cur);
  ecs_meta_member(&cur, field_name);
  return ecs_meta_get_bool(&cur);
}

// =============================================================================
// Field metadata access - get offsets and sizes from Flecs
// =============================================================================

// Get the number of members in a struct component
inline int32_t vybe_get_member_count(ecs_world_t* w, ecs_entity_t comp) {
  const EcsStruct* st = ecs_get(w, comp, EcsStruct);
  if (!st) return 0;
  return ecs_vec_count(&st->members);
}

// Get the offset of a member by index
inline int32_t vybe_get_member_offset(ecs_world_t* w, ecs_entity_t comp, int32_t idx) {
  const EcsStruct* st = ecs_get(w, comp, EcsStruct);
  if (!st) return -1;
  if (idx < 0 || idx >= ecs_vec_count(&st->members)) return -1;
  const ecs_member_t* m = ecs_vec_get_t(&st->members, ecs_member_t, idx);
  return m->offset;
}

// Get the size of a member by index
inline int32_t vybe_get_member_size(ecs_world_t* w, ecs_entity_t comp, int32_t idx) {
  const EcsStruct* st = ecs_get(w, comp, EcsStruct);
  if (!st) return -1;
  if (idx < 0 || idx >= ecs_vec_count(&st->members)) return -1;
  const ecs_member_t* m = ecs_vec_get_t(&st->members, ecs_member_t, idx);
  return m->size;
}

// Get the name of a member by index (returns empty string if not found)
inline const char* vybe_get_member_name(ecs_world_t* w, ecs_entity_t comp, int32_t idx) {
  const EcsStruct* st = ecs_get(w, comp, EcsStruct);
  if (!st) return "";
  if (idx < 0 || idx >= ecs_vec_count(&st->members)) return "";
  const ecs_member_t* m = ecs_vec_get_t(&st->members, ecs_member_t, idx);
  return m->name ? m->name : "";
}

// Get the total size of a component
inline int32_t vybe_get_comp_size(ecs_world_t* w, ecs_entity_t comp) {
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  return ti ? ti->size : 0;
}

// =============================================================================
// Direct memory access - use offsets for O(1) field access
// =============================================================================

// Get component pointer for an entity (returns NULL if entity doesn't have it)
inline void* vybe_get_comp_ptr(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp) {
  // First check if entity has the component
  if (!ecs_has_id(w, e, comp)) return nullptr;
  const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
  if (!ti || ti->size == 0) return nullptr;
  return ecs_ensure_id(w, e, comp, ti->size);
}

// Get const component pointer for an entity (returns NULL if entity doesn't have it)
inline const void* vybe_get_comp_ptr_const(ecs_world_t* w, ecs_entity_t e, ecs_entity_t comp) {
  if (!ecs_has_id(w, e, comp)) return nullptr;
  return ecs_get_id(w, e, comp);
}

// Direct set float at offset
inline void vybe_set_float_at(void* base, int32_t offset, double value) {
  if (!base) return;
  *(float*)((char*)base + offset) = (float)value;
}

// Direct set double at offset
inline void vybe_set_double_at(void* base, int32_t offset, double value) {
  if (!base) return;
  *(double*)((char*)base + offset) = value;
}

// Direct set i32 at offset
inline void vybe_set_i32_at(void* base, int32_t offset, int64_t value) {
  if (!base) return;
  *(int32_t*)((char*)base + offset) = (int32_t)value;
}

// Direct set i64 at offset
inline void vybe_set_i64_at(void* base, int32_t offset, int64_t value) {
  if (!base) return;
  *(int64_t*)((char*)base + offset) = value;
}

// Direct set u32 at offset
inline void vybe_set_u32_at(void* base, int32_t offset, uint64_t value) {
  if (!base) return;
  *(uint32_t*)((char*)base + offset) = (uint32_t)value;
}

// Direct set u64 at offset
inline void vybe_set_u64_at(void* base, int32_t offset, uint64_t value) {
  if (!base) return;
  *(uint64_t*)((char*)base + offset) = value;
}

// Direct set bool at offset
inline void vybe_set_bool_at(void* base, int32_t offset, bool value) {
  if (!base) return;
  *(bool*)((char*)base + offset) = value;
}

// Direct get float at offset
inline double vybe_get_float_at(const void* base, int32_t offset) {
  if (!base) return 0.0;
  return (double)*(const float*)((const char*)base + offset);
}

// Direct get double at offset
inline double vybe_get_double_at(const void* base, int32_t offset) {
  if (!base) return 0.0;
  return *(const double*)((const char*)base + offset);
}

// Direct get i32 at offset
inline int64_t vybe_get_i32_at(const void* base, int32_t offset) {
  if (!base) return 0;
  return (int64_t)*(const int32_t*)((const char*)base + offset);
}

// Direct get i64 at offset
inline int64_t vybe_get_i64_at(const void* base, int32_t offset) {
  if (!base) return 0;
  return *(const int64_t*)((const char*)base + offset);
}

// Direct get u32 at offset
inline uint64_t vybe_get_u32_at(const void* base, int32_t offset) {
  if (!base) return 0;
  return (uint64_t)*(const uint32_t*)((const char*)base + offset);
}

// Direct get u64 at offset
inline uint64_t vybe_get_u64_at(const void* base, int32_t offset) {
  if (!base) return 0;
  return *(const uint64_t*)((const char*)base + offset);
}

// Direct get bool at offset
inline bool vybe_get_bool_at(const void* base, int32_t offset) {
  if (!base) return false;
  return *(const bool*)((const char*)base + offset);
}

// =============================================================================
// Standalone instance memory allocation
// =============================================================================

// Allocate memory for a standalone component instance
inline void* vybe_alloc_comp(int32_t size) {
  return calloc(1, size);  // Zero-initialized
}

// Free standalone component memory
inline void vybe_free_comp(void* ptr) {
  if (ptr) free(ptr);
}

// =============================================================================
// Pointer conversion helper
// =============================================================================

// Convert a pointer to int64 for use as a stable cache key
inline int64_t vybe_type_ptr_to_int64(ecs_world_t* ptr) {
  return reinterpret_cast<int64_t>(ptr);
}

#endif // VYBE_TYPE_HELPERS_H
