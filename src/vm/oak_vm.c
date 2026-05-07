#include "internal/oak_vm.h"

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
    oak_map_set(map, subscript, value);
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
    oak_value_decref(recv);
    oak_value_decref(subscript);
    oak_value_decref(value);
    return OAK_VM_RUNTIME_ERROR;
  }
  if (!oak_is_i32(subscript))
  {
    oak_vm_runtime_error(vm,
                         "array index must be an integer, got %s",
                         oak_vm_value_kind_desc(subscript));
    oak_value_decref(recv);
    oak_value_decref(subscript);
    oak_value_decref(value);
    return OAK_VM_RUNTIME_ERROR;
  }
  struct oak_obj_array_t* arr = oak_as_array(recv);
  const int i = oak_as_i32(subscript);
  if (i < 0 || (usize)i >= arr->length)
  {
    oak_vm_runtime_error(
        vm, "array index %d out of bounds (length %zu)", i, arr->length);
    oak_value_decref(recv);
    oak_value_decref(subscript);
    oak_value_decref(value);
    return OAK_VM_RUNTIME_ERROR;
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
                                   : oak_map_value_at(map, (usize)i); /* OAK_OP_MAP_VAL_AT */
  const enum oak_vm_result_t pr = oak_vm_push(vm, v);
  oak_value_decref(iter_index);
  oak_value_decref(map_val);
  return pr;
}

void oak_vm_init(struct oak_vm_t* vm)
{
  vm->chunk = null;
  vm->ip = null;
  vm->sp = vm->stack;
  vm->stack_base = 0;
  vm->frame_count = 0;
  vm->modules = null;
}

void oak_vm_set_module_registry(struct oak_vm_t* vm,
                                struct oak_module_registry_t* modules)
{
  vm->modules = modules;
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
  /* Reject empty / non-terminated chunks at entry so the dispatch loop can
   * trust that HALT (or RETURN) eventually fires without a per-iteration IP
   * bounds check. */
  if (chunk->count == 0)
  {
    oak_log(OAK_LOG_ERROR, "vm: empty chunk");
    return OAK_VM_RUNTIME_ERROR;
  }

  vm->chunk = chunk;
  vm->ip = chunk->bytecode;

  for (;;)
  {
    const u8 instruction = oak_vm_read_u8(vm);
    switch (instruction)
    {
      case OAK_OP_HALT:
        return OAK_VM_OK;
      case OAK_OP_CONSTANT:
      {
        const u16 idx = oak_vm_read_u16(vm);
        oak_assert((usize)idx < chunk->const_count);
        OAK_VM_TRY(oak_vm_push(vm, chunk->constants[idx]));
        break;
      }
      case OAK_OP_PUSH_INT8:
      {
        const signed char val = (signed char)oak_vm_read_u8(vm);
        OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_I32((int)val)));
        break;
      }
      case OAK_OP_TRUE:
        OAK_VM_TRY(oak_vm_push(vm, OAK_VALUE_BOOL(1)));
        break;
      case OAK_OP_FALSE:
        OAK_VM_TRY(oak_vm_push(vm, OAK_VALUE_BOOL(0)));
        break;
      case OAK_OP_POP:
      {
        const struct oak_value_t val = oak_vm_pop(vm);
        oak_value_decref(val);
        break;
      }
      case OAK_OP_POP_N:
      {
        const u8 n = oak_vm_read_u8(vm);
        oak_assert((usize)(vm->sp - vm->stack) >= (usize)n);
        for (u8 i = 0; i < n; ++i)
          oak_value_decref(*--vm->sp);
        break;
      }
      case OAK_OP_GET_LOCAL:
      {
        const u8 slot = oak_vm_read_u8(vm);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        OAK_VM_TRY(oak_vm_push(vm, vm->stack[idx]));
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const u8 slot = oak_vm_read_u8(vm);
        const usize idx = vm->stack_base + (usize)slot;
        if (idx >= OAK_STACK_MAX)
        {
          oak_vm_runtime_error(vm, "local slot out of range (slot %u)", slot);
          return OAK_VM_RUNTIME_ERROR;
        }
        const struct oak_value_t new_val = oak_vm_pop(vm);
        const struct oak_value_t old_val = vm->stack[idx];
        vm->stack[idx] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_INC_LOCAL:
      case OAK_OP_DEC_LOCAL:
      {
        const u8 slot = oak_vm_read_u8(vm);
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
      case OAK_OP_BINARY:
      {
        const u8 binop = oak_vm_read_u8(vm);
        const struct oak_value_t b = oak_vm_pop(vm);
        const struct oak_value_t a = oak_vm_pop(vm);

        switch (binop)
        {
          case OAK_BINOP_ADD:
          case OAK_BINOP_SUBTRACT:
          case OAK_BINOP_MULTIPLY:
          case OAK_BINOP_DIVIDE:
          case OAK_BINOP_MODULO:
          {
            if (binop == OAK_BINOP_ADD && oak_is_string(a) && oak_is_string(b))
            {
              struct oak_obj_string_t* result =
                  oak_string_concat(oak_as_string(a), oak_as_string(b));
              const enum oak_vm_result_t pr =
                  oak_vm_push_owned(vm, OAK_VALUE_OBJ(result));
              oak_value_decref(a);
              oak_value_decref(b);
              if (pr != OAK_VM_OK)
              {
                oak_obj_decref(&result->obj);
                return pr;
              }
              break;
            }
            const enum oak_vm_result_t r =
                oak_vm_numeric_binary(vm, binop, a, b);
            oak_value_decref(a);
            oak_value_decref(b);
            if (r != OAK_VM_OK)
              return r;
            break;
          }
          case OAK_BINOP_EQUAL:
          case OAK_BINOP_NOT_EQUAL:
          {
            const int eq = oak_value_equal(a, b);
            oak_value_decref(a);
            oak_value_decref(b);
            OAK_VM_TRY(oak_vm_push(
                vm, OAK_VALUE_BOOL(binop == OAK_BINOP_EQUAL ? eq : !eq)));
            break;
          }
          case OAK_BINOP_LESS:
          case OAK_BINOP_LESS_EQUAL:
          case OAK_BINOP_GREATER:
          case OAK_BINOP_GREATER_EQUAL:
          {
            const enum oak_vm_result_t r =
                oak_vm_numeric_compare(vm, binop, a, b);
            oak_value_decref(a);
            oak_value_decref(b);
            if (r != OAK_VM_OK)
              return r;
            break;
          }
          default:
            oak_value_decref(a);
            oak_value_decref(b);
            oak_vm_runtime_error(
                vm, "internal error: unknown binop (0x%02x)", binop);
            return OAK_VM_RUNTIME_ERROR;
        }
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
        const struct oak_value_t result =
            oak_is_i32(val) ? OAK_VALUE_I32(-oak_as_i32(val))
                            : OAK_VALUE_F32(-oak_as_f32(val));
        oak_value_decref(val);
        OAK_VM_TRY(oak_vm_push(vm, result));
        break;
      }
      case OAK_OP_NOT:
      {
        struct oak_value_t val = oak_vm_pop(vm);
        const struct oak_value_t result = OAK_VALUE_BOOL(!oak_is_truthy(val));
        oak_value_decref(val);
        OAK_VM_TRY(oak_vm_push(vm, result));
        break;
      }
      case OAK_OP_BOOL:
      {
        struct oak_value_t val = oak_vm_pop(vm);
        const struct oak_value_t result = OAK_VALUE_BOOL(oak_is_truthy(val));
        oak_value_decref(val);
        OAK_VM_TRY(oak_vm_push(vm, result));
        break;
      }
      case OAK_OP_JUMP:
      {
        const u16 offset = oak_vm_read_u16(vm);
        vm->ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const u16 offset = oak_vm_read_u16(vm);
        struct oak_value_t cond = oak_vm_pop(vm);
        if (!oak_is_truthy(cond))
          vm->ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_JUMP_IF_TRUE:
      {
        const u16 offset = oak_vm_read_u16(vm);
        struct oak_value_t cond = oak_vm_pop(vm);
        if (oak_is_truthy(cond))
          vm->ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const u16 offset = oak_vm_read_u16(vm);
        vm->ip -= offset;
        break;
      }
      case OAK_OP_CALL:
      {
        const enum oak_vm_result_t r = oak_vm_op_call(vm);
        if (r != OAK_VM_OK)
          return r;
        /* OP_CALL may switch chunks for cross-module calls; refresh the
         * local pointer so subsequent reads see the active chunk. */
        chunk = vm->chunk;
        break;
      }
      case OAK_OP_RETURN:
      {
        const enum oak_vm_result_t r = oak_vm_op_return(vm);
        if (r != OAK_VM_OK)
          return r;
        chunk = vm->chunk;
        break;
      }
      case OAK_OP_NEW_ARR:
      {
        const u8 count = oak_vm_read_u8(vm);
        oak_assert((usize)(vm->sp - vm->stack) >= (usize)count);
        struct oak_obj_array_t* arr = oak_array_new();
        struct oak_value_t* base = vm->sp - (int)count;
        for (int i = 0; i < (int)count; ++i)
          oak_array_push(arr, base[i]);
        for (int i = 0; i < (int)count; ++i)
          oak_value_decref(base[i]);
        vm->sp -= (int)count;
        OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_OBJ(&arr->obj)));
        break;
      }
      case OAK_OP_NEW_MAP:
      {
        const u8 count = oak_vm_read_u8(vm);
        const usize slots = (usize)count * 2u;
        oak_assert((usize)(vm->sp - vm->stack) >= slots);
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
        OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_OBJ(&map->obj)));
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
            (int)count, type_name, (const char* const*)lay->name, null);
        for (int i = 0; i < (int)count; ++i)
        {
          oak_value_incref(base[i]);
          s->fields[i] = base[i];
        }
        for (int i = 0; i < (int)count; ++i)
          oak_value_decref(base[i]);
        oak_value_decref(type_name_val);
        vm->sp -= (int)count + 1;
        OAK_VM_TRY(oak_vm_push_owned(vm, OAK_VALUE_OBJ(&s->obj)));
        break;
      }
      case OAK_OP_GET_FIELD:
      {
        const u8 idx = oak_vm_read_u8(vm);
        const struct oak_value_t recv = oak_vm_pop(vm);
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
          OAK_VM_TRY(oak_vm_push_owned(vm, result));
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
          if ((int)idx >= ns->type->field_count)
          {
            oak_vm_runtime_error(vm,
                                 "field index %u out of bounds (native record "
                                 "'%s' has %d fields)",
                                 (unsigned)idx,
                                 ns->type->name,
                                 ns->type->field_count);
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
          ns->type->fields[idx].setter(recv, value);
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
              vm,
              "internal: cross-module reference resolves to invalid slot");
          return OAK_VM_RUNTIME_ERROR;
        }
        OAK_VM_TRY(oak_vm_push(vm, m->chunk->constants[const_idx]));
        break;
      }
      default:
        oak_vm_runtime_error(
            vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}

struct oak_src_loc_t
oak_vm_oak_mem_src_loc(const struct oak_vm_t* vm)
{
#ifndef OAK_TRACK_MEMORY
  (void)vm;
  return (struct oak_src_loc_t){ 0 };
#else
  if (!vm || !vm->chunk || !vm->chunk->bytecode || !vm->chunk->debug ||
      !vm->chunk->debug->locations)
    return (struct oak_src_loc_t){
      .file = null,
      .line = 0,
    };
  if (vm->ip < vm->chunk->bytecode)
  {
    return (struct oak_src_loc_t){
      .file = null,
      .line = 0,
    };
  }
  const usize ip_off = (usize)(vm->ip - vm->chunk->bytecode);
  if (ip_off < 2u)
  {
    return (struct oak_src_loc_t){
      .file = null,
      .line = 0,
    };
  }
  const usize call_off = ip_off - 2u;
  if (call_off >= vm->chunk->count)
  {
    return (struct oak_src_loc_t){
      .file = null,
      .line = 0,
    };
  }
  const struct oak_code_loc_t cloc = vm->chunk->debug->locations[call_off];
  return (struct oak_src_loc_t){
    .file = vm->chunk->debug->source_name,
    .line = cloc.line,
  };
#endif
}
