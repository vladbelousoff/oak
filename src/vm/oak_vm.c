#include "internal/oak_vm.h"

void oak_vm_init(struct oak_vm_t* vm, struct oak_allocator_t* allocator)
{
  vm->chunk = null;
  vm->ip = null;
  vm->sp = vm->stack;
  vm->stack_base = 0;
  vm->frame_count = 0;
  vm->modules = null;
  vm->allocator = allocator;
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
      case OAK_OP_PUSH_INT8:
      case OAK_OP_TRUE:
      case OAK_OP_FALSE:
      case OAK_OP_POP:
      case OAK_OP_POP_N:
      case OAK_OP_GET_LOCAL:
      case OAK_OP_SET_LOCAL:
      case OAK_OP_WEAKEN:
      case OAK_OP_INC_LOCAL:
      case OAK_OP_DEC_LOCAL:
      {
        const enum oak_vm_result_t r =
            vm_stack_dispatch(vm, chunk, instruction);
        if (r != OAK_VM_OK)
          return r;
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
          case OAK_BINOP_INT_DIVIDE:
          case OAK_BINOP_MODULO:
          {
            if (binop == OAK_BINOP_ADD && oak_is_string(a) && oak_is_string(b))
            {
              struct oak_obj_string_t* result =
                  oak_string_concat(vm->allocator, oak_as_string(a), oak_as_string(b));
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
        const struct oak_value_t result = oak_is_i32(val)
                                              ? OAK_VALUE_I32(-oak_as_i32(val))
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
      case OAK_OP_JUMP_IF_FALSE:
      case OAK_OP_JUMP_IF_TRUE:
      case OAK_OP_LOOP:
      {
        const enum oak_vm_result_t r = vm_control_dispatch(vm, instruction);
        if (r != OAK_VM_OK)
          return r;
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

      case OAK_OP_CALL_VIRTUAL:
      {
        const enum oak_vm_result_t r = oak_vm_op_call_virtual(vm);
        if (r != OAK_VM_OK)
          return r;
        chunk = vm->chunk;
        break;
      }

      case OAK_OP_NEW_ARR:
      case OAK_OP_NEW_MAP:
      case OAK_OP_GET_INDEX:
      case OAK_OP_SET_INDEX:
      case OAK_OP_MAP_KEY_AT:
      case OAK_OP_MAP_VAL_AT:
      case OAK_OP_NEW_RECORD:
      case OAK_OP_GET_FIELD:
      case OAK_OP_SET_FIELD:
      case OAK_OP_GET_MODULE_FN:
      case OAK_OP_MAKE_TRAIT_OBJECT:
      {
        const enum oak_vm_result_t r =
            vm_object_dispatch(vm, chunk, instruction);
        if (r != OAK_VM_OK)
          return r;
        break;
      }

      default:
        oak_vm_runtime_error(
            vm, "internal error: unknown opcode 0x%02x", instruction);
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}

struct oak_src_loc_t oak_vm_oak_mem_src_loc(const struct oak_vm_t* vm)
{
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
}
