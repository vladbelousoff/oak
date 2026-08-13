/*
 * The native-callback support surface: everything a bound C function calls on
 * the oak_native_call_t it was handed.
 *
 * It lives under src/vm/ rather than next to the descriptors in
 * src/runtime/oak_bind.c because all of it ultimately reports through
 * oak_vm_runtime_error, which is internal to the VM.
 */

#include "internal/oak_vm.h"

#include <stdarg.h>
#include <stdio.h>

oak_fn_call_result_t oak_native_error(oak_native_call_t* call,
                                      const char* fmt,
                                      ...)
{
  char buf[384];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (!call || !call->vm)
  {
    /* No VM to record against -- a native invoked outside a call, or a test
     * harness driving the callback directly.  Log it so the reason is not
     * lost entirely. */
    oak_log(OAK_LOG_ERROR, "error: %s", buf);
    return OAK_FN_CALL_RUNTIME_ERROR;
  }

  if (call->fn_name)
    oak_vm_runtime_error(call->vm, "%s: %s", call->fn_name, buf);
  else
    oak_vm_runtime_error(call->vm, "%s", buf);
  return OAK_FN_CALL_RUNTIME_ERROR;
}


/* Every accessor below funnels its rejection through here, so the phrasing is
 * identical whichever type was wanted. */
static int arg_reject(oak_native_call_t* call,
                      const oak_value_t* args,
                      const usize argc,
                      const usize i,
                      const char* want)
{
  if (i >= argc)
    oak_native_error(call,
                     "argument %zu: expected %s, but only %zu were passed",
                     i,
                     want,
                     argc);
  else
    oak_native_error(call,
                     "argument %zu: expected %s, found %s",
                     i,
                     want,
                     oak_vm_value_kind_desc(args[i]));
  return 0;
}

/* Non-zero when args[i] exists. Every accessor checks this before touching the
 * slot: the VM matches argc against the binding's arity before dispatching, but
 * an accessor is a public entry point and must not read off the end regardless
 * of how it was reached. */
static int arg_present(const usize argc, const usize i)
{
  return i < argc;
}

int oak_arg_i32(oak_native_call_t* call,
                const oak_value_t* args,
                const usize argc,
                const usize i,
                int* out)
{
  if (!arg_present(argc, i) || !oak_is_i32(args[i]))
    return arg_reject(call, args, argc, i, "an integer");
  *out = oak_as_i32(args[i]);
  return 1;
}

int oak_arg_f32(oak_native_call_t* call,
                const oak_value_t* args,
                const usize argc,
                const usize i,
                float* out)
{
  if (!arg_present(argc, i) || !oak_is_f32(args[i]))
    return arg_reject(call, args, argc, i, "a float");
  *out = oak_as_f32(args[i]);
  return 1;
}

int oak_arg_number(oak_native_call_t* call,
                   const oak_value_t* args,
                   const usize argc,
                   const usize i,
                   float* out)
{
  if (!arg_present(argc, i) || !oak_is_number(args[i]))
    return arg_reject(call, args, argc, i, "a number");
  *out = oak_is_f32(args[i]) ? oak_as_f32(args[i]) : (float)oak_as_i32(args[i]);
  return 1;
}

int oak_arg_bool(oak_native_call_t* call,
                 const oak_value_t* args,
                 const usize argc,
                 const usize i,
                 int* out)
{
  if (!arg_present(argc, i) || !oak_is_bool(args[i]))
    return arg_reject(call, args, argc, i, "a bool");
  *out = oak_as_bool(args[i]) ? 1 : 0;
  return 1;
}

int oak_arg_cstring(oak_native_call_t* call,
                    const oak_value_t* args,
                    const usize argc,
                    const usize i,
                    const char** out)
{
  if (!arg_present(argc, i) || !oak_is_string(args[i]))
    return arg_reject(call, args, argc, i, "a string");
  *out = oak_as_cstring(args[i]);
  return 1;
}

int oak_arg_string(oak_native_call_t* call,
                   const oak_value_t* args,
                   const usize argc,
                   const usize i,
                   const oak_obj_string_t** out)
{
  if (!arg_present(argc, i) || !oak_is_string(args[i]))
    return arg_reject(call, args, argc, i, "a string");
  *out = oak_as_string(args[i]);
  return 1;
}

int oak_arg_native(oak_native_call_t* call,
                   const oak_value_t* args,
                   const usize argc,
                   const usize i,
                   const oak_bind_type_t* type,
                   void** out)
{
  if (!type)
    return arg_reject(call, args, argc, i, "a native record");
  if (!arg_present(argc, i) || !oak_is_native_record(args[i]))
    return arg_reject(call, args, argc, i, type->name);

  const oak_obj_native_record_t* record = oak_as_native_record(args[i]);
  if (record->type != type)
  {
    oak_native_error(call,
                     "argument %zu: expected %s, found %s",
                     i,
                     type->name,
                     record->type && record->type->name ? record->type->name
                                                        : "another native type");
    return 0;
  }
  if (!record->instance)
  {
    oak_native_error(call, "argument %zu: %s has no instance", i, type->name);
    return 0;
  }
  *out = record->instance;
  return 1;
}

int oak_arg_self(oak_native_call_t* call,
                 const oak_value_t* args,
                 const usize argc,
                 void** out)
{
  if (!call || !call->self_type)
  {
    oak_native_error(call, "no receiver: not bound as an instance method");
    return 0;
  }
  return oak_arg_native(call, args, argc, 0, call->self_type, out);
}

oak_value_t oak_native_self_new(oak_native_call_t* call, void* instance)
{
  if (!call || !call->vm || !call->self_type)
    return OAK_VALUE_NONE;
  return oak_vm_native_record_new(call->vm, call->self_type, instance);
}
