#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* How a native function is reached from Oak.
 *
 * FREE is the zero value, so a descriptor naming only a function and its
 * implementation is a free function -- the common case, and the one a table of
 * module-scoped functions writes dozens of times.  A method has to say so.
 *
 * Its own header because both sides of the binding need it: oak_bind_fn_t
 * (oak_bind.h) is where an embedder sets it, and oak_obj_native_fn_t
 * (oak_value.h) carries it to the callback as oak_native_call_t::kind, which
 * is what lets oak_arg_self tell a receiver from a first argument.  oak_bind.h
 * already includes oak_value.h through oak_native.h, so the shared enum has to
 * sit below both -- the same reason oak_type_kind.h exists. */
typedef enum oak_bind_fn_kind oak_bind_fn_kind_t;
enum oak_bind_fn_kind
{
  /* A free function: `fn()` bound globally, `module.fn()` bound in a module.
   * Has no receiver type. */
  OAK_BIND_FN_FREE = 0,
  OAK_BIND_FN_INSTANCE_METHOD,
  OAK_BIND_FN_STATIC_METHOD,
};

#ifdef __cplusplus
}
#endif
