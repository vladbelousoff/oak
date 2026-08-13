#pragma once

#include "oak_export.h"
#include "oak_value.h"

typedef struct oak_compile_options oak_compile_options_t;

/* Math built-ins. These implementations are registered into the global scope by
 * the compiler (see oak_compiler_builtins.c); there is no separate `math`
 * module to import. */
oak_fn_call_result_t oak_math_sqrt(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_sin(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_cos(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_tan(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_abs(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_fmod(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_min(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_max(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_random(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_floor(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_ceil(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_round(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_pow(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_log(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_exp(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_atan2(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
oak_fn_call_result_t oak_math_sign(oak_native_call_t* call, const oak_value_t* args, int argc, oak_value_t* out);
