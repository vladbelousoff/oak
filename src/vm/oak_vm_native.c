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
