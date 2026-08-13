#pragma once

#include "oak_export.h"
#include "oak_value.h"

typedef struct oak_compile_options oak_compile_options_t;

/* String built-ins.
 *
 * The instance methods below are registered by the compiler as string methods
 * (reachable through `text.method(...)` syntax, see oak_compiler_method_table.c)
 * and receive the receiver string as args[0]. The `oak_str_*` free functions
 * (ord/chr/parse_number) are registered into the global scope as ordinary
 * builtins; there is no separate `string` module to import. */

/* Instance methods (receiver is args[0]). */
oak_fn_call_result_t oak_str_upper(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_lower(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_trim(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_contains(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_starts_with(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_ends_with(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_index_of(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_replace(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_repeat(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_substring(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_to_snake_case(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_to_camel_case(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);

/* Global builtins. */
oak_fn_call_result_t oak_str_ord(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_chr(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
oak_fn_call_result_t oak_str_parse_number(oak_native_call_t* call, const oak_value_t* args, const usize argc, oak_value_t* out);
