#include "internal/oak_vm.h"

static enum oak_vm_result_t runtime_error_decref2(struct oak_value_t a,
                                                  struct oak_value_t b)
{
  oak_value_decref(a);
  oak_value_decref(b);
  return OAK_VM_RUNTIME_ERROR;
}

static enum oak_vm_result_t runtime_error_decref3(struct oak_value_t a,
                                                  struct oak_value_t b,
                                                  struct oak_value_t c)
{
  oak_value_decref(a);
  oak_value_decref(b);
  oak_value_decref(c);
  return OAK_VM_RUNTIME_ERROR;
}

static enum oak_vm_result_t vm_op_get_index_impl(struct oak_vm_t* vm)
{
  const struct oak_value_t subscript = oak_vm_pop(vm);
  const struct oak_value_t recv = oak_vm_pop(vm);

  if (oak_is_map(recv))
  {
    const struct oak_obj_map_t* map = oak_as_map(recv);
    struct oak_value_t out;
    if (!oak_map_get(map, subscript, &out))
    {
      oak_vm_runtime_error(vm, "key not found in map");
      return runtime_error_decref2(subscript, recv);
    }
    const enum oak_vm_result_t pr = oak_vm_push(vm, out);
    oak_value_decref(subscript);
    oak_value_decref(recv);
    return pr;
  }

  if (!oak_is_array(recv))
  {
    oak_vm_runtime_error(vm,
                         "indexing requires an array or map, got %s",
                         oak_vm_value_kind_desc(recv));
    return runtime_error_decref2(subscript, recv);
  }
  if (!oak_is_i32(subscript))
  {
    oak_vm_runtime_error(vm,
                         "array index must be an integer, got %s",
                         oak_vm_value_kind_desc(subscript));
    return runtime_error_decref2(subscript, recv);
  }
  const struct oak_obj_array_t* arr = oak_as_array(recv);
  const int i = oak_as_i32(subscript);
  if (i < 0 || (usize)i >= arr->length)
  {
    oak_vm_runtime_error(
        vm, "array index %d out of bounds (length %zu)", i, arr->length);
    return runtime_error_decref2(subscript, recv);
  }
  const enum oak_vm_result_t pr = oak_vm_push(vm, arr->items[i]);
  oak_value_decref(subscript);
  oak_value_decref(recv);
  return pr;
}

static enum oak_vm_result_t vm_op_set_index_impl(struct oak_vm_t* vm)
{
  if (vm->sp - vm->stack < 3)
  {
    oak_vm_runtime_error(vm, "stack underflow in indexed assignment");
    return OAK_VM_RUNTIME_ERROR;
  }
  const struct oak_value_t value = oak_vm_pop(vm);
  const struct oak_value_t subscript = oak_vm_pop(vm);
  const struct oak_value_t recv = oak_vm_pop(vm);

  if (oak_is_map(recv))
  {
    struct oak_obj_map_t* map = oak_as_map(recv);
    if (!oak_map_set(map, subscript, value))
    {
      oak_vm_runtime_error(vm, "invalid map key: %s",
                           oak_vm_value_kind_desc(subscript));
      return runtime_error_decref3(recv, subscript, value);
    }
    oak_value_decref(recv);
    oak_value_decref(subscript);
    oak_value_decref(value);
    return OAK_VM_OK;
  }

  if (!oak_is_array(recv))
  {
    oak_vm_runtime_error(vm,
                         "indexed assignment requires an array or map, got %s",
                         oak_vm_value_kind_desc(recv));
    return runtime_error_decref3(recv, subscript, value);
  }
  if (!oak_is_i32(subscript))
  {
    oak_vm_runtime_error(vm,
                         "array index must be an integer, got %s",
                         oak_vm_value_kind_desc(subscript));
    return runtime_error_decref3(recv, subscript, value);
  }
  struct oak_obj_array_t* arr = oak_as_array(recv);
  const int i = oak_as_i32(subscript);
  if (i < 0 || (usize)i >= arr->length)
  {
    oak_vm_runtime_error(
        vm, "array index %d out of bounds (length %zu)", i, arr->length);
    return runtime_error_decref3(recv, subscript, value);
  }
  oak_value_decref(arr->items[i]);
  arr->items[i] = value;
  oak_value_decref(recv);
  oak_value_decref(subscript);
  return OAK_VM_OK;
}

static enum oak_vm_result_t vm_op_map_key_value_at(struct oak_vm_t* vm,
                                                   const u8 instruction)
{
  const struct oak_value_t iter_index = oak_vm_pop(vm);
  const struct oak_value_t map_val = oak_vm_pop(vm);

  if (!oak_is_map(map_val))
  {
    oak_vm_runtime_error(vm,
                         "map iteration requires a map, got %s",
                         oak_vm_value_kind_desc(map_val));
    return runtime_error_decref2(iter_index, map_val);
  }
  if (!oak_is_i32(iter_index))
  {
    oak_vm_runtime_error(vm,
                         "map iterator index must be an integer, got %s",
                         oak_vm_value_kind_desc(iter_index));
    return runtime_error_decref2(iter_index, map_val);
  }
  const struct oak_obj_map_t* map = oak_as_map(map_val);
  const int i = oak_as_i32(iter_index);
  if (i < 0 || (usize)i >= map->length)
  {
    oak_vm_runtime_error(
        vm, "map iterator index %d out of bounds (length %zu)", i, map->length);
    return runtime_error_decref2(iter_index, map_val);
  }
  const struct oak_value_t v =
      instruction == OAK_OP_MAP_KEY_AT
          ? oak_map_key_at(map, (usize)i)
          : oak_map_value_at(map, (usize)i); /* OAK_OP_MAP_VAL_AT */
  const enum oak_vm_result_t pr = oak_vm_push(vm, v);
  oak_value_decref(iter_index);
  oak_value_decref(map_val);
  return pr;
}

enum oak_vm_result_t vm_object_dispatch(struct oak_vm_t* vm,
                                        struct oak_chunk_t* chunk,
                                        u8 instruction)
{
  switch (instruction)
  {
    case OAK_OP_NEW_ARR:
    {
      const u8 count = oak_vm_read_u8(vm);
      oak_assert((usize)(vm->sp - vm->stack) >= (usize)count);
      struct oak_obj_array_t* arr = oak_array_new(vm->allocator);
      struct oak_value_t* base = vm->sp - (int)count;
      for (int i = 0; i < (int)count; ++i)
        oak_array_push(arr, base[i]);
      for (int i = 0; i < (int)count; ++i)
        oak_value_decref(base[i]);
      vm->sp -= (int)count;
      const enum oak_vm_result_t result =
          oak_vm_push_owned(vm, OAK_VALUE_OBJ(&arr->obj));
      if (result != OAK_VM_OK)
        return result;
      break;
    }
    case OAK_OP_NEW_MAP:
    {
      const u8 count = oak_vm_read_u8(vm);
      const usize slots = (usize)count * 2u;
      oak_assert((usize)(vm->sp - vm->stack) >= slots);
      struct oak_obj_map_t* map = oak_map_new(vm->allocator);
      struct oak_value_t* base = vm->sp - (int)slots;
      for (int i = 0; i < (int)count; ++i)
      {
        const struct oak_value_t k = base[i * 2 + 0];
        const struct oak_value_t v = base[i * 2 + 1];
        if (!oak_map_set(map, k, v))
        {
          oak_vm_runtime_error(vm, "invalid map key: %s",
                               oak_vm_value_kind_desc(k));
          for (usize j = 0; j < slots; ++j)
            oak_value_decref(base[j]);
          vm->sp -= (int)slots;
          oak_obj_decref(&map->obj);
          return OAK_VM_RUNTIME_ERROR;
        }
      }
      for (usize i = 0; i < slots; ++i)
        oak_value_decref(base[i]);
      vm->sp -= (int)slots;
      const enum oak_vm_result_t result =
          oak_vm_push_owned(vm, OAK_VALUE_OBJ(&map->obj));
      if (result != OAK_VM_OK)
        return result;
      break;
    }
    case OAK_OP_GET_INDEX:
    {
      const enum oak_vm_result_t r = vm_op_get_index_impl(vm);
      if (r != OAK_VM_OK)
        return r;
      break;
    }
    case OAK_OP_SET_INDEX:
    {
      const enum oak_vm_result_t r = vm_op_set_index_impl(vm);
      if (r != OAK_VM_OK)
        return r;
      break;
    }
    case OAK_OP_MAP_KEY_AT:
    case OAK_OP_MAP_VAL_AT:
    {
      const enum oak_vm_result_t r = vm_op_map_key_value_at(vm, instruction);
      if (r != OAK_VM_OK)
        return r;
      break;
    }
    case OAK_OP_NEW_RECORD:
    {
      /* Stack on entry: [..., type_name_string, f0, f1, ..., f(N-1)].
       * Result: [..., record]. */
      const u8 count = oak_vm_read_u8(vm);
      const u16 layout_id = oak_vm_read_u16(vm);
      oak_assert((usize)(vm->sp - vm->stack) >= (usize)count + 1u);
      oak_assert((usize)layout_id < (usize)chunk->field_layout_count);
      const struct oak_chunk_field_layout* const lay =
          &chunk->field_layouts[layout_id];
      oak_assert(lay->field_count == (int)count);

      struct oak_value_t* base = vm->sp - (int)count;
      const struct oak_value_t type_name_val = base[-1];
      const char* type_name = null;
      if (oak_is_string(type_name_val))
        type_name = oak_as_string(type_name_val)->chars;

      struct oak_obj_record_t* s = oak_record_new(
          vm->allocator, (int)count, type_name, (const char* const*)lay->name);
      for (int i = 0; i < (int)count; ++i)
      {
        oak_value_assert_can_refcopy_to_table(base[i], s->obj.table_id);
        oak_value_incref(base[i]);
        s->fields[i] = base[i];
      }
      for (int i = 0; i < (int)count; ++i)
        oak_value_decref(base[i]);
      oak_value_decref(type_name_val);
      vm->sp -= (int)count + 1;
      const enum oak_vm_result_t result =
          oak_vm_push_owned(vm, OAK_VALUE_OBJ(&s->obj));
      if (result != OAK_VM_OK)
        return result;
      break;
    }
    case OAK_OP_GET_FIELD:
    {
      const u8 idx = oak_vm_read_u8(vm);
      const struct oak_value_t recv = oak_vm_pop(vm);
      if (oak_is_none_like(recv))
      {
        oak_vm_runtime_error(vm, "field access on none (expired weak reference)");
        oak_value_decref(recv);
        return OAK_VM_RUNTIME_ERROR;
      }
      if (oak_is_record(recv))
      {
        const struct oak_obj_record_t* s = oak_as_record(recv);
        oak_assert((int)idx < s->field_count);
        const struct oak_value_t field = s->fields[idx];
        const enum oak_vm_result_t pr = oak_vm_push(vm, field);
        oak_value_decref(recv);
        if (pr != OAK_VM_OK)
          return pr;
      }
      else if (oak_is_native_record(recv))
      {
        const struct oak_obj_native_record_t* ns = oak_as_native_record(recv);
        if ((int)idx >= oak_dynarr_count(ns->type->fields))
        {
          oak_vm_runtime_error(vm,
                               "field index %u out of bounds (native record "
                               "'%s' has %d fields)",
                               (unsigned)idx,
                               ns->type->name,
                               oak_dynarr_count(ns->type->fields));
          oak_value_decref(recv);
          return OAK_VM_RUNTIME_ERROR;
        }
        /* Getter returns an owned reference; push without extra incref. */
        const struct oak_value_t result = ns->type->fields[idx].getter(
            recv, ns->type->fields[idx].user_data);
        oak_value_decref(recv);
        const enum oak_vm_result_t push_result =
            oak_vm_push_owned(vm, result);
        if (push_result != OAK_VM_OK)
          return push_result;
      }
      else
      {
        oak_vm_runtime_error(vm,
                             "field access requires a record, got %s",
                             oak_vm_value_kind_desc(recv));
        oak_value_decref(recv);
        return OAK_VM_RUNTIME_ERROR;
      }
      break;
    }
    case OAK_OP_SET_FIELD:
    {
      /* Stack: [..., recv, value]; pops both. */
      const u8 idx = oak_vm_read_u8(vm);
      oak_assert(vm->sp - vm->stack >= 2);
      const struct oak_value_t value = oak_vm_pop(vm);
      const struct oak_value_t recv = oak_vm_pop(vm);
      if (oak_is_none_like(recv))
      {
        oak_vm_runtime_error(vm, "field assignment on none (expired weak reference)");
        oak_value_decref(recv);
        oak_value_decref(value);
        return OAK_VM_RUNTIME_ERROR;
      }
      if (oak_is_record(recv))
      {
        struct oak_obj_record_t* s = oak_as_record(recv);
        oak_assert((int)idx < s->field_count);
        const struct oak_value_t old_field = s->fields[idx];
        s->fields[idx] = value;
        oak_value_decref(old_field);
        oak_value_decref(recv);
      }
      else if (oak_is_native_record(recv))
      {
        const struct oak_obj_native_record_t* ns = oak_as_native_record(recv);
        if ((int)idx >= oak_dynarr_count(ns->type->fields))
        {
          oak_vm_runtime_error(vm,
                               "field index %u out of bounds (native record "
                               "'%s' has %d fields)",
                               (unsigned)idx,
                               ns->type->name,
                               oak_dynarr_count(ns->type->fields));
          oak_value_decref(recv);
          oak_value_decref(value);
          return OAK_VM_RUNTIME_ERROR;
        }
        if (!ns->type->fields[idx].setter)
        {
          oak_vm_runtime_error(
              vm,
              "field '%s' on native record '%s' is read-only",
              ns->type->fields[idx].name,
              ns->type->name);
          oak_value_decref(recv);
          oak_value_decref(value);
          return OAK_VM_RUNTIME_ERROR;
        }
        ns->type->fields[idx].setter(
            recv, value, ns->type->fields[idx].user_data);
        oak_value_decref(recv);
        oak_value_decref(value);
      }
      else
      {
        oak_vm_runtime_error(vm,
                             "field assignment requires a record, got %s",
                             oak_vm_value_kind_desc(recv));
        oak_value_decref(recv);
        oak_value_decref(value);
        return OAK_VM_RUNTIME_ERROR;
      }
      break;
    }
    case OAK_OP_GET_MODULE_FN:
    {
      const u16 mod_id = oak_vm_read_u16(vm);
      const u16 const_idx = oak_vm_read_u16(vm);
      if (!vm->modules)
      {
        oak_vm_runtime_error(vm,
                             "internal: cross-module reference but VM has "
                             "no module registry");
        return OAK_VM_RUNTIME_ERROR;
      }
      const struct oak_module_t* m =
          oak_module_registry_get(vm->modules, mod_id);
      if (!m || !m->chunk || (usize)const_idx >= m->chunk->const_count)
      {
        oak_vm_runtime_error(
            vm, "internal: cross-module reference resolves to invalid slot");
        return OAK_VM_RUNTIME_ERROR;
      }
      const enum oak_vm_result_t result =
          oak_vm_push(vm, m->chunk->constants[const_idx]);
      if (result != OAK_VM_OK)
        return result;
      break;
    }
    case OAK_OP_MAKE_INTERFACE_OBJECT:
    {
      const u16 vtable_idx = oak_vm_read_u16(vm);
      if ((usize)vtable_idx >= chunk->const_count)
      {
        oak_vm_runtime_error(vm,
                             "MAKE_INTERFACE_OBJECT: vtable index out of range");
        return OAK_VM_RUNTIME_ERROR;
      }
      const struct oak_value_t vtable_val = chunk->constants[vtable_idx];
      if (!oak_is_array(vtable_val))
      {
        oak_vm_runtime_error(vm,
                             "MAKE_INTERFACE_OBJECT: vtable is not an array");
        return OAK_VM_RUNTIME_ERROR;
      }
      struct oak_obj_array_t* vtable = oak_as_array(vtable_val);
      const struct oak_value_t concrete = oak_vm_pop(vm);
      struct oak_obj_interface_object_t* to = oak_interface_object_new(vm->allocator, concrete, vtable);
      oak_value_decref(concrete);
      const enum oak_vm_result_t result =
          oak_vm_push_owned(vm, OAK_VALUE_OBJ(&to->obj));
      if (result != OAK_VM_OK)
        return result;
      break;
    }
    default:
      oak_vm_runtime_error(
          vm, "internal error: unknown opcode 0x%02x", instruction);
      return OAK_VM_RUNTIME_ERROR;
  }
  return OAK_VM_OK;
}
