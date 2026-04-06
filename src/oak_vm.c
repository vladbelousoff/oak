#include "oak_vm.h"

void oak_vm_init(oak_vm_t* vm)
{
  vm->chunk = NULL;
  vm->ip = NULL;
  vm->sp = vm->stack;
}

void oak_vm_free(oak_vm_t* vm)
{
  while (vm->sp > vm->stack)
  {
    --vm->sp;
    oak_value_decref(*vm->sp);
  }

  vm->chunk = NULL;
  vm->ip = NULL;
}

static void vm_push(oak_vm_t* vm, const oak_value_t value)
{
  oak_assert(vm->sp < vm->stack + OAK_STACK_MAX);
  oak_value_incref(value);
  *vm->sp++ = value;
}

static oak_value_t vm_pop(oak_vm_t* vm)
{
  oak_assert(vm->sp > vm->stack);
  return *--vm->sp;
}

static oak_value_t vm_peek(const oak_vm_t* vm, const int distance)
{
  return vm->sp[-1 - distance];
}

static void runtime_error(const oak_vm_t* vm, const char* msg)
{
  const size_t offset = (size_t)(vm->ip - vm->chunk->bytecode - 1);
  const int line = vm->chunk->lines[offset];
  oak_log(OAK_LOG_ERR, "runtime error [line %d]: %s", line, msg);
}

static inline uint16_t vm_read(oak_vm_t* vm, const int n)
{
  uint16_t val = 0;
  for (int i = 0; i < n; i++)
    val = (val << 8) | *vm->ip++;
  return val;
}

static oak_vm_result_t numeric_binary(oak_vm_t* vm,
                                      const uint8_t op,
                                      const oak_value_t a,
                                      const oak_value_t b)
{
  if (oak_is_i32(a) && oak_is_i32(b))
  {
    int result;
    switch (op)
    {
      case OAK_OP_ADD:
        result = oak_as_i32(a) + oak_as_i32(b);
        break;
      case OAK_OP_SUB:
        result = oak_as_i32(a) - oak_as_i32(b);
        break;
      case OAK_OP_MUL:
        result = oak_as_i32(a) * oak_as_i32(b);
        break;
      case OAK_OP_DIV:
        if (oak_as_i32(b) == 0)
        {
          runtime_error(vm, "division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) / oak_as_i32(b);
        break;
      case OAK_OP_MOD:
        if (oak_as_i32(b) == 0)
        {
          runtime_error(vm, "modulo by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = oak_as_i32(a) % oak_as_i32(b);
        break;
      default:
        runtime_error(vm, "unexpected numeric op");
        return OAK_VM_RUNTIME_ERROR;
    }
    vm_push(vm, OAK_VALUE_I32(result));
    return OAK_VM_OK;
  }

  if ((oak_is_i32(a) || oak_is_f32(a)) && (oak_is_i32(b) || oak_is_f32(b)))
  {
    const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
    const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);

    float result;
    switch (op)
    {
      case OAK_OP_ADD:
        result = fa + fb;
        break;
      case OAK_OP_SUB:
        result = fa - fb;
        break;
      case OAK_OP_MUL:
        result = fa * fb;
        break;
      case OAK_OP_DIV:
        if (fb == 0.0f)
        {
          runtime_error(vm, "division by zero");
          return OAK_VM_RUNTIME_ERROR;
        }
        result = fa / fb;
        break;
      case OAK_OP_MOD:
        runtime_error(vm, "modulo not supported on floats");
        return OAK_VM_RUNTIME_ERROR;
      default:
        runtime_error(vm, "unexpected numeric op");
        return OAK_VM_RUNTIME_ERROR;
    }

    vm_push(vm, OAK_VALUE_F32(result));
    return OAK_VM_OK;
  }

  runtime_error(vm, "operands must be numbers");
  return OAK_VM_RUNTIME_ERROR;
}

static oak_vm_result_t numeric_compare(oak_vm_t* vm,
                                       const uint8_t op,
                                       const oak_value_t a,
                                       const oak_value_t b)
{
  if (!((oak_is_i32(a) || oak_is_f32(a)) && (oak_is_i32(b) || oak_is_f32(b))))
  {
    runtime_error(vm, "operands must be numbers for comparison");
    return OAK_VM_RUNTIME_ERROR;
  }

  const float fa = oak_is_f32(a) ? oak_as_f32(a) : (float)oak_as_i32(a);
  const float fb = oak_is_f32(b) ? oak_as_f32(b) : (float)oak_as_i32(b);

  int result;
  switch (op)
  {
    case OAK_OP_LT:
      result = fa < fb;
      break;
    case OAK_OP_LE:
      result = fa <= fb;
      break;
    case OAK_OP_GT:
      result = fa > fb;
      break;
    case OAK_OP_GE:
      result = fa >= fb;
      break;
    default:
      runtime_error(vm, "unexpected comparison op");
      return OAK_VM_RUNTIME_ERROR;
  }

  vm_push(vm, OAK_VALUE_BOOL(result));
  return OAK_VM_OK;
}

oak_vm_result_t oak_vm_run(oak_vm_t* vm, oak_chunk_t* chunk)
{
  vm->chunk = chunk;
  vm->ip = chunk->bytecode;

  for (;;)
  {
    const uint8_t instruction = vm_read(vm, 1);
    switch (instruction)
    {
      case OAK_OP_HALT:
        return OAK_VM_OK;
      case OAK_OP_CONSTANT:
      {
        const uint8_t idx = vm_read(vm, 1);
        vm_push(vm, chunk->constants[idx]);
        break;
      }
      case OAK_OP_TRUE:
        vm_push(vm, OAK_VALUE_BOOL(1));
        break;
      case OAK_OP_FALSE:
        vm_push(vm, OAK_VALUE_BOOL(0));
        break;
      case OAK_OP_POP:
      {
        const oak_value_t val = vm_pop(vm);
        oak_value_decref(val);
        break;
      }
      case OAK_OP_GET_LOCAL:
      {
        const uint8_t slot = vm_read(vm, 1);
        vm_push(vm, vm->stack[slot]);
        break;
      }
      case OAK_OP_SET_LOCAL:
      {
        const uint8_t slot = vm_read(vm, 1);
        const oak_value_t old_val = vm->stack[slot];
        const oak_value_t new_val = vm_peek(vm, 0);
        oak_value_incref(new_val);
        vm->stack[slot] = new_val;
        oak_value_decref(old_val);
        break;
      }
      case OAK_OP_ADD:
      {
        const oak_value_t b = vm_pop(vm);
        const oak_value_t a = vm_pop(vm);

        if (oak_is_string(a) && oak_is_string(b))
        {
          oak_obj_string_t* result =
              oak_string_concat(oak_as_string(a), oak_as_string(b));
          const oak_value_t value = OAK_VALUE_OBJ(result);
          vm_push(vm, value);
          oak_obj_decref(&result->obj);
          oak_value_decref(a);
          oak_value_decref(b);
          break;
        }

        const oak_vm_result_t r = numeric_binary(vm, OAK_OP_ADD, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_SUB:
      case OAK_OP_MUL:
      case OAK_OP_DIV:
      case OAK_OP_MOD:
      {
        const oak_value_t b = vm_pop(vm);
        const oak_value_t a = vm_pop(vm);
        const oak_vm_result_t r = numeric_binary(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_NEGATE:
      {
        oak_value_t val = vm_pop(vm);
        if (oak_is_i32(val))
        {
          vm_push(vm, OAK_VALUE_I32(-oak_as_i32(val)));
        }
        else if (oak_is_f32(val))
        {
          vm_push(vm, OAK_VALUE_F32(-oak_as_f32(val)));
        }
        else
        {
          oak_value_decref(val);
          runtime_error(vm, "operand must be a number");
          return OAK_VM_RUNTIME_ERROR;
        }
        oak_value_decref(val);
        break;
      }
      case OAK_OP_NOT:
      {
        oak_value_t val = vm_pop(vm);
        vm_push(vm, OAK_VALUE_BOOL(!oak_is_truthy(val)));
        oak_value_decref(val);
        break;
      }
      case OAK_OP_EQ:
      {
        oak_value_t b = vm_pop(vm);
        oak_value_t a = vm_pop(vm);
        vm_push(vm, OAK_VALUE_BOOL(oak_value_equal(a, b)));
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_NEQ:
      {
        oak_value_t b = vm_pop(vm);
        oak_value_t a = vm_pop(vm);
        vm_push(vm, OAK_VALUE_BOOL(!oak_value_equal(a, b)));
        oak_value_decref(a);
        oak_value_decref(b);
        break;
      }
      case OAK_OP_LT:
      case OAK_OP_LE:
      case OAK_OP_GT:
      case OAK_OP_GE:
      {
        oak_value_t b = vm_pop(vm);
        oak_value_t a = vm_pop(vm);
        oak_vm_result_t r = numeric_compare(vm, instruction, a, b);
        oak_value_decref(a);
        oak_value_decref(b);
        if (r != OAK_VM_OK)
          return r;
        break;
      }
      case OAK_OP_JUMP:
      {
        const uint16_t offset = vm_read(vm, 2);
        vm->ip += offset;
        break;
      }
      case OAK_OP_JUMP_IF_FALSE:
      {
        const uint16_t offset = vm_read(vm, 2);
        oak_value_t cond = vm_pop(vm);
        if (!oak_is_truthy(cond))
          vm->ip += offset;
        oak_value_decref(cond);
        break;
      }
      case OAK_OP_LOOP:
      {
        const uint16_t offset = vm_read(vm, 2);
        vm->ip -= offset;
        break;
      }
      case OAK_OP_PRINT:
      {
        oak_value_t val = vm_pop(vm);
        oak_value_print(val);
        oak_value_decref(val);
        break;
      }
      default:
        runtime_error(vm, "unknown opcode");
        return OAK_VM_RUNTIME_ERROR;
    }
  }
}
