#pragma once

#include "oak_export.h"
#include "oak_value.h"

struct oak_compile_options_t;

/* String built-ins.
 *
 * The instance methods below are registered by the compiler as string methods
 * (reachable through `text.method(...)` syntax, see oak_compiler_method_table.c)
 * and receive the receiver string as args[0]. The `oak_str_*` free functions
 * (ord/chr/parse_number) are registered into the global scope as ordinary
 * builtins; there is no separate `string` module to import. */

/* Instance methods (receiver is args[0]). */
OAK_API enum oak_fn_call_result_t oak_str_upper(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_lower(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_trim(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_contains(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_starts_with(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_ends_with(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_index_of(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_replace(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_repeat(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_substring(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_to_snake_case(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_to_camel_case(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);

/* Global builtins. */
OAK_API enum oak_fn_call_result_t oak_str_ord(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_chr(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
OAK_API enum oak_fn_call_result_t oak_str_parse_number(struct oak_native_ctx_t* ctx, const struct oak_value_t* args, int argc, struct oak_value_t* out);
