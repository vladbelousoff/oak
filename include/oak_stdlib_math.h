#pragma once

#include "oak_export.h"
#include "oak_value.h"

struct oak_compile_options_t;

OAK_API void oak_stdlib_register_math(struct oak_compile_options_t* opts);

OAK_API enum oak_fn_call_result_t oak_math_sqrt(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_sin(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_cos(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_tan(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_abs(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_fmod(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_min(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_max(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_math_random(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
