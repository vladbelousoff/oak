#pragma once

#include "oak_module.h"
#include "oak_parser.h"
#include "oak_types.h"

struct oak_compiler_t;

/* Resolve an import alias to the module it refers to.
 * Returns null when name is not an import alias in the current module. */
const struct oak_module_t*
oak_compiler_module_for_alias(const struct oak_compiler_t* c,
                              const char* name,
                              usize name_len);

/* Look up an exported function on an imported module identified by alias.
 * Sets *out_mod when the alias resolves (even when the function is missing),
 * so callers can cite mod->dotted_name in error messages.
 * Returns null when alias is unknown or the function is not exported. */
const struct oak_module_export_fn_t*
oak_compiler_module_export_fn(const struct oak_compiler_t* c,
                              const char* alias,
                              usize alias_len,
                              const char* fn_name,
                              usize fn_name_len,
                              const struct oak_module_t** out_mod);

/* Pattern detector: does `node` have the shape `IDENT.IDENT` where the lhs
 * IDENT is a known import alias?  If so, returns the resolved module and
 * writes the rhs token (e.g. the enum-type name in `color.Color`) to
 * *out_member.  Returns null when the pattern does not match. */
const struct oak_module_t*
oak_compiler_match_module_member(const struct oak_compiler_t* c,
                                 const struct oak_ast_node_t* node,
                                 const struct oak_token_t** out_member);

/* Read at most `cap` IMPORT_PATH segments into out_segs[].
 * Returns the total number of segments (may exceed cap if the path is longer).
 * Used by record-literal compilation to split `Type` vs `mod.Type`. */
int oak_compiler_import_path_segments(const struct oak_ast_node_t* path_node,
                                      const struct oak_ast_node_t** out_segs,
                                      int cap);
