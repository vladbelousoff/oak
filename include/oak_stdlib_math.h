#pragma once

#include "oak_export.h"
#include "oak_value.h"

struct oak_compile_options_t;

/* Math built-ins. These implementations are registered into the global scope by
 * the compiler (see oak_compiler_builtins.c); there is no separate `math`
 * module to import. */
OAK_API enum oak_fn_call_result_t oak_math_sqrt(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_sin(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_cos(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_tan(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_abs(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_fmod(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_min(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_max(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_random(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_floor(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_ceil(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_round(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_pow(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_log(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_exp(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_atan2(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_sign(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
