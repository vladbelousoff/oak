#include "oak_vm_internal.h"

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
      oak_value_decref(subscript);
      oak_value_decref(recv);
      return OAK_VM_RUNTIME_ERROR;
    }
    oak_vm_push(vm, out);
    oak_value_decref(subscript);
    oak_value_decref(recv);
    return OAK_VM_OK;
  }

  if (!oak_is_array(recv))
  {
    oak_vm_runtime_error(vm,
                         "indexing requires an array or map, got %s",
                         oak_vm_value_kind_desc(recv));
    oak_value_decref(subscript);
    oak_value_decref(recv);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_is_i32(subscript))
  {
    oak_vm_runtime_error(vm,
                         "array index must be an integer, got %s",
                         oak_vm_value_kind_desc(subscript));
    oak_value_decref(subscript);
    oak_value_decref(recv);
    return OAK_VM_RUNTIME_ERROR;
  }
  const struct oak_obj_array_t* arr = oak_as_array(recv);
  const int i = oak_as_i32(subscript);
  if (i < 0 || (usize)i >= arr->length)
  {
    oak_vm_runtime_error(
        vm, "array index %d out of bounds (length %zu)", i, arr->length);
    oak_value_decref(subscript);
    oak_value_decref(recv);
    return OAK_VM_RUNTIME_ERROR;
  }
  oak_vm_push(vm, arr->items[i]);
  oak_value_decref(subscript);
  oak_value_decref(recv);
  return OAK_VM_OK;
}

static enum oak_vm_result_t vm_op_set_index_impl(struct oak_vm_t* vm)
{
  if (vm->sp - vm->stack < 3)
  {
    oak_vm_runtime_error(vm, "stack underflow in indexed assignment");
    return OAK_VM_RUNTIME_ERROR;
  }
  const struct oak_value_t value = vm->sp[-1];
  const struct oak_value_t subscript = vm->sp[-2];
  const struct oak_value_t recv = vm->sp[-3];

  if (oak_is_map(recv))
  {
    struct oak_obj_map_t* map = oak_as_map(recv);
    oak_map_set(map, subscript, value);
    oak_value_decref(recv);
    oak_value_decref(subscript);
    vm->sp[-3] = value;
    vm->sp -= 2;
    return OAK_VM_OK;
  }

  if (!oak_is_array(recv))
  {
    oak_vm_runtime_error(vm,
                         "indexed assignment requires an array or map, got %s",
                         oak_vm_value_kind_desc(recv));
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_is_i32(subscript))
  {
    oak_vm_runtime_error(vm,
                         "array index must be an integer, got %s",
                         oak_vm_value_kind_desc(subscript));
    return OAK_VM_RUNTIME_ERROR;
  }
  struct oak_obj_array_t* arr = oak_as_array(recv);
  const int i = oak_as_i32(subscript);
  if (i < 0 || (usize)i >= arr->length)
  {
    oak_vm_runtime_error(
        vm, "array index %d out of bounds (length %zu)", i, arr->length);
    return OAK_VM_RUNTIME_ERROR;
  }
  oak_value_decref(arr->items[i]);
  oak_value_incref(value);
  arr->items[i] = value;
  oak_value_decref(recv);
  oak_value_decref(subscript);
  vm->sp[-3] = value;
  vm->sp -= 2;
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
    oak_value_decref(iter_index);
    oak_value_decref(map_val);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_is_i32(iter_index))
  {
    oak_vm_runtime_error(vm,
                         "map iterator index must be an integer, got %s",
                         oak_vm_value_kind_desc(iter_index));
    oak_value_decref(iter_index);
    oak_value_decref(map_val);
    return OAK_VM_RUNTIME_ERROR;
  }
  const struct oak_obj_map_t* map = oak_as_map(map_val);
  const int i = oak_as_i32(iter_index);
  if (i < 0 || (usize)i >= map->length)
  {
    oak_vm_runtime_error(
        vm, "map iterator index %d out of bounds (length %zu)", i, map->length);
    oak_value_decref(iter_index);
    oak_value_decref(map_val);
    return OAK_VM_RUNTIME_ERROR;
  }
  const struct oak_value_t v = instruction == OAK_OP_MAP_KEY_AT
                                   ? oak_map_key_at(map, (usize)i)
                                   : oak_map_value_at(map, (usize)i);
  oak_vm_push(vm, v);
  oak_value_decref(iter_index);
  oak_value_decref(map_val);
  return OAK_VM_OK;
}

void oak_vm_init(struct oak_vm_t* vm)
{
  vm->chunk = null;
  vm->ip = null;
  vm->sp = vm->stack;
  vm->stack_base = 0;
  vm->frame_count = 0;
  vm->had_stack_overflow = 0;
}

void oak_vm_free(struct oak_vm_t* vm)
{
  while (vm->sp > vm->stack)
  {
    --vm->sp;
    oak_value_decref(*vm->sp);
  }

  vm->chunk = null;
  vm->ip = null;
}

enum oak_vm_result_t oak_vm_run(struct oak_vm_t* vm, struct oak_chunk_t* chunk)
{
  vm->chunk = chunk;
  vm->ip = chunk->bytecode;

  for (;;)
  {
    if (vm->had_stack_overflow)
    {
      vm->had_stack_overflow = 0;
      oak_vm_runtime_error(vm, "stack overflow (max %d values)", OAK_STACK_MAX);
      return OAK_VM_RUNTIME_ERROR;
    }

    if (vm->ip < chunk->bytecode || vm->ip >= chunk->bytecode + chunk->count)
    {
      oak_log(OAK_LOG_ERROR, "vm: instruction pointer out of bounds");
      return OAK_VM_RUNTIME_ERROR;
    }

    const u8 instruction = oak_vm_read(vm, 1);
    switch (instruction)
    {
      case OAK_OP_HALT:
        return OAK_VM_OK;
      case OAK_OP_CONSTANT:
      {
        const u8 idx = oak_vm_read(vm, 1);
        if ((usize)idx >= chunk->const_count)
        {
          oak_vm_runtime_error(vm,
                               "constant index %u out of range (%zu constants)",
                               (unsigned)idx,
                               chunk->const_count);
          return OAK_VM_RUNTIME_ERROR;
        }
        oak_vm_push(vm, chunk->constants[idx]);
        break;
      }
      case OAK_OP_CONSTANT_LONG:
      {
        const u16 idx = oak_vm_read_u16(vm);
        if ((usize)idx >= chunk->const_count)
        {
          oak_vm_runtime_error(vm,
                               "constant index %u out of range (%zu constants)",
                               (unsigned)idx,
                               chunk->const_count);
          return OAK_VM_RUNTIME_ERROR;
        }
        oak_vm_push(vm, chunk->constants[idx]);
        break;
      }
      case OAK_OP_TRUE:
        oak_vm_push(vm, OAK_VALUE_BOOL(1));
        break;
      case OAK_OP_FALSE:
        oak_vm_push(vm, OAK_VALUE_BOOL(0));
        break;
      case OAK_OP_POP:
      {
        const struct oak_value_t val = oak_vm_pop(vm);
        oak_value_decref(val);
        break;
      }
      case OAK_OP_GET_LOCAL:
      {
        const u8 slot = oak_vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        oak_vm_push(vm, vm->stack[idx]);
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const u8 slot = oak_vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t old_val = vm->stack[idx];
        const struct oak_value_t new_val = oak_vm_peek(vm, 0);
        oak_value_incref(new_val);
        vm->stack[idx] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_INC_LOCAL:
      case OAK_OP_DEC_LOCAL:
      {
        const u8 slot = oak_vm_read(vm, 1);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t val = vm->stack[idx];
        if (!oak_is_number(val))
        {
          oak_vm_runtime_error(
              vm,
              "local increment/decrement expects a number, got %s",
              oak_vm_value_kind_desc(val));
          return OAK_VM_RUNTIME_ERROR;
        }
        if (oak_is_i32(val))
        {
          const int delta = instruction == OAK_OP_INC_LOCAL ? 1 : -1;
          vm->stack[idx] = OAK_VALUE_I32(oak_as_i32(val) + delta);
          break;
        }
        const float fdelta = instruction == OAK_OP_INC_LOCAL ? 1.0f : -1.0f;
        vm->stack[idx] = OAK_VALUE_F32(oak_as_f32(val) + fdelta);
        break;
      }
      case OAK_OP_ADD:
      case OAK_OP_SUB:
      case OAK_OP_MUL:
      case OAK_OP_DIV:
      case OAK_OP_MOD:
      {
        const struct oak_value_t b = oak_vm_pop(vm);
        const struct oak_value_t a = oak_vm_pop(vm);

        if (instruction == OAK_OP_ADD && oak_is_string(a) && oak_is_string(b))
        {
          struct oak_obj_string_t* result =
              oak_string_concat(oak_as_string(a), oak_as_string(b));
          oak_vm_push_owned(vm, OAK_VALUE_OBJ(result));
          oak_value_decref(a);
          oak_value_decref(b);
          break;
        }

        const enum oak_vm_result_t r =
            oak_vm_numeric_binary(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_NEGATE:
      {
        struct oak_value_t val = oak_vm_pop(vm);
        if (!oak_is_number(val))
        {
          oak_value_decref(val);
          oak_vm_runtime_error(vm,
                               "unary '-' expects a number, got %s",
                               oak_vm_value_kind_desc(val));
          return OAK_VM_RUNTIME_ERROR;
        }
        if (oak_is_i32(val))
          oak_vm_push(vm, OAK_VALUE_I32(-oak_as_i32(val)));
        else
          oak_vm_push(vm, OAK_VALUE_F32(-oak_as_f32(val)));
        oak_value_decref(val);
        break;
      }
      case OAK_OP_NOT:
      {
        struct oak_value_t val = oak_vm_pop(vm);
        oak_vm_push(vm, OAK_VALUE_BOOL(!oak_is_truthy(val)));
        oak_value_decref(val);
        break;
      }
      case OAK_OP_EQ:
      case OAK_OP_NEQ:
      {
        struct oak_value_t b = oak_vm_pop(vm);
        struct oak_value_t a = oak_vm_pop(vm);
        int eq = oak_value_equal(a, b);
        oak_vm_push(vm, OAK_VALUE_BOOL(instruction == OAK_OP_EQ ? eq : !eq));
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_LT:
      case OAK_OP_LE:
      case OAK_OP_GT:
      case OAK_OP_GE:
      {
        struct oak_value_t b = oak_vm_pop(vm);
        struct oak_value_t a = oak_vm_pop(vm);
        enum oak_vm_result_t r = oak_vm_numeric_compare(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_JUMP:
      {
        const u32 offset = oak_vm_read_u32(vm);
        vm->ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const u32 offset = oak_vm_read_u32(vm);
        struct oak_value_t cond = oak_vm_pop(vm);
        if (!oak_is_truthy(cond))
          vm->ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const u32 offset = oak_vm_read_u32(vm);
        vm->ip -= offset;
        break;
      }
      case OAK_OP_CALL:
      {
        const enum oak_vm_result_t r = oak_vm_op_call(vm);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_RETURN:
      {
        const enum oak_vm_result_t r = oak_vm_op_return(vm);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_NEW_ARRAY:
      {
        struct oak_obj_array_t* arr = oak_array_new();
        oak_vm_push_owned(vm, OAK_VALUE_OBJ(&arr->obj));
        break;
      }
      case OAK_OP_NEW_ARRAY_FROM_STACK:
      {
        const u8 count = (u8)oak_vm_read(vm, 1);
        if ((usize)(vm->sp - vm->stack) < (usize)count)
        {
          oak_vm_runtime_error(vm, "stack underflow in array literal");
          return OAK_VM_RUNTIME_ERROR;
        }
        struct oak_obj_array_t* arr = oak_array_new();
        struct oak_value_t* base = vm->sp - (int)count;
        for (int i = 0; i < (int)count; ++i)
          oak_array_push(arr, base[i]);
        for (int i = 0; i < (int)count; ++i)
          oak_value_decref(base[i]);
        vm->sp -= (int)count;
        oak_vm_push_owned(vm, OAK_VALUE_OBJ(&arr->obj));
        break;
      }
      case OAK_OP_NEW_MAP:
      {
        struct oak_obj_map_t* map = oak_map_new();
        oak_vm_push_owned(vm, OAK_VALUE_OBJ(&map->obj));
        break;
      }
      case OAK_OP_NEW_MAP_FROM_STACK:
      {
        const u8 count = (u8)oak_vm_read(vm, 1);
        const usize slots = (usize)count * 2u;
        if ((usize)(vm->sp - vm->stack) < slots)
        {
          oak_vm_runtime_error(vm, "stack underflow in map literal");
          return OAK_VM_RUNTIME_ERROR;
        }
        struct oak_obj_map_t* map = oak_map_new();
        struct oak_value_t* base = vm->sp - (int)slots;
        for (int i = 0; i < (int)count; ++i)
        {
          const struct oak_value_t k = base[i * 2 + 0];
          const struct oak_value_t v = base[i * 2 + 1];
          oak_map_set(map, k, v);
        }
        for (usize i = 0; i < slots; ++i)
          oak_value_decref(base[i]);
        vm->sp -= (int)slots;
        oak_vm_push_owned(vm, OAK_VALUE_OBJ(&map->obj));
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
      case OAK_OP_MAP_VALUE_AT:
      {
        const enum oak_vm_result_t r = vm_op_map_key_value_at(vm, instruction);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_NEW_RECORD_FROM_STACK:
      {
        /* Stack on entry: [..., type_name_string, f0, f1, ..., f(N-1)].
         * Result: [..., record]. */
        const u8 count = (u8)oak_vm_read(vm, 1);
        if ((usize)(vm->sp - vm->stack) < (usize)count + 1u)
        {
          oak_vm_runtime_error(vm, "stack underflow in record literal");
          return OAK_VM_RUNTIME_ERROR;
        }
        struct oak_value_t* base = vm->sp - (int)count;
        const struct oak_value_t type_name_val = base[-1];
        const char* type_name = null;
        if (oak_is_string(type_name_val))
          type_name = oak_as_string(type_name_val)->chars;

        struct oak_obj_record_t* s = oak_record_new((int)count, type_name);
        for (int i = 0; i < (int)count; ++i)
        {
          oak_value_incref(base[i]);
          s->fields[i] = base[i];
        }
        for (int i = 0; i < (int)count; ++i)
          oak_value_decref(base[i]);
        oak_value_decref(type_name_val);
        vm->sp -= (int)count + 1;
        oak_vm_push_owned(vm, OAK_VALUE_OBJ(&s->obj));
        break;
      }
      case OAK_OP_GET_FIELD:
      {
        const u8 idx = (u8)oak_vm_read(vm, 1);
        const struct oak_value_t recv = oak_vm_pop(vm);
        if (oak_is_record(recv))
        {
          const struct oak_obj_record_t* s = oak_as_record(recv);
          if ((int)idx >= s->field_count)
          {
            oak_vm_runtime_error(
                vm,
                "field index %u out of bounds (record has %d fields)",
                (unsigned)idx,
                s->field_count);
            oak_value_decref(recv);
            return OAK_VM_RUNTIME_ERROR;
          }
          oak_vm_push(vm, s->fields[idx]);
          oak_value_decref(recv);
        }
        else if (oak_is_native_record(recv))
        {
          const struct oak_obj_native_record_t* ns = oak_as_native_record(recv);
          if ((int)idx >= ns->type->field_count)
          {
            oak_vm_runtime_error(vm,
                                 "field index %u out of bounds (native record "
                                 "'%s' has %d fields)",
                                 (unsigned)idx,
                                 ns->type->name,
                                 ns->type->field_count);
            oak_value_decref(recv);
            return OAK_VM_RUNTIME_ERROR;
          }
          /* Getter returns an owned reference; push without extra incref. */
          const struct oak_value_t result = ns->type->fields[idx].getter(recv);
          oak_value_decref(recv);
          oak_vm_push_owned(vm, result);
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
        /* Stack: [..., recv, value]; result: [..., value]. */
        const u8 idx = (u8)oak_vm_read(vm, 1);
        if (vm->sp - vm->stack < 2)
        {
          oak_vm_runtime_error(vm, "stack underflow in field assignment");
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t value = vm->sp[-1];
        const struct oak_value_t recv = vm->sp[-2];
        if (oak_is_record(recv))
        {
          struct oak_obj_record_t* s = oak_as_record(recv);
          if ((int)idx >= s->field_count)
          {
            oak_vm_runtime_error(
                vm,
                "field index %u out of bounds (record has %d fields)",
                (unsigned)idx,
                s->field_count);
            return OAK_VM_RUNTIME_ERROR;
          }
          oak_value_decref(s->fields[idx]);
          oak_value_incref(value);
          s->fields[idx] = value;
          oak_value_decref(recv);
          vm->sp[-2] = value;
        }
        else if (oak_is_native_record(recv))
        {
          const struct oak_obj_native_record_t* ns = oak_as_native_record(recv);
          if ((int)idx >= ns->type->field_count)
          {
            oak_vm_runtime_error(vm,
                                 "field index %u out of bounds (native record "
                                 "'%s' has %d fields)",
                                 (unsigned)idx,
                                 ns->type->name,
                                 ns->type->field_count);
            return OAK_VM_RUNTIME_ERROR;
          }
          if (!ns->type->fields[idx].setter)
          {
            oak_vm_runtime_error(
                vm,
                "field '%s' on native record '%s' is read-only",
                ns->type->fields[idx].name,
                ns->type->name);
            return OAK_VM_RUNTIME_ERROR;
          }
          ns->type->fields[idx].setter(recv, value);
          oak_value_decref(recv);
          vm->sp[-2] = value;
        }
        else
        {
          oak_vm_runtime_error(vm,
                               "field assignment requires a record, got %s",
                               oak_vm_value_kind_desc(recv));
          return OAK_VM_RUNTIME_ERROR;
        }
        vm->sp -= 1;
        break;
      }
      default:
        oak_vm_runtime_error(
            vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}
