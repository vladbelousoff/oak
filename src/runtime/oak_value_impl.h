#pragma once

/*
 * VM-facing value allocation, internal to the library.
 *
 * Every constructor here takes an explicit object table.  Table 0 is the
 * process-shared table; a nonzero table must belong to a live VM.  The public
 * oak_*_new functions in oak_value.h are the table-0 wrappers over these, and
 * the oak_vm_*_new functions in oak_vm.h are the wrappers that pass the
 * calling VM's table.
 *
 * These are deliberately not OAK_API: an embedder selects an owner through
 * the oak_vm_* entry points, never by naming a table id.
 */

#include "oak_refcount_ops.h"
#include "oak_value.h"

oak_obj_string_t*
oak_string_new_len(oak_allocator_t* a, const char* chars, usize length);

oak_obj_string_t* oak_string_new_in_table(oak_allocator_t* a,
                                          u32 table_id,
                                          const char* chars);

oak_obj_string_t* oak_string_new_len_in_table(oak_allocator_t* a,
                                              u32 table_id,
                                              const char* chars,
                                              usize length);

oak_obj_string_t* oak_string_concat_in_table(oak_allocator_t* a,
                                             u32 table_id,
                                             const oak_obj_string_t* s1,
                                             const oak_obj_string_t* s2);

oak_obj_array_t* oak_array_new_in_table(oak_allocator_t* a, u32 table_id);

oak_obj_record_t* oak_record_new_in_table(oak_allocator_t* a,
                                          u32 table_id,
                                          int field_count,
                                          const char* type_name,
                                          const char* const* field_names);

oak_obj_native_record_t*
oak_obj_native_record_new_in_table(oak_allocator_t* a,
                                   u32 table_id,
                                   const oak_bind_type_t* type,
                                   void* instance);

oak_obj_interface_object_t*
oak_interface_object_new_in_table(oak_allocator_t* a,
                                  u32 table_id,
                                  oak_value_t value,
                                  oak_obj_array_t* vtable);

oak_obj_map_t* oak_map_new_in_table(oak_allocator_t* a, u32 table_id);

oak_obj_string_t* oak_value_to_string_in_table(oak_allocator_t* allocator,
                                               u32 table_id,
                                               oak_value_t value);

oak_obj_string_t*
oak_string_from_value_repr_in_table(oak_allocator_t* allocator,
                                    u32 table_id,
                                    oak_value_t value);
