#pragma once

#include "oak_export.h"
#include "oak_module_impl.h"
#include "oak_parser.h"
#include "oak_types.h"

typedef struct oak_compiler oak_compiler_t;

/* Resolve an import alias to the module it refers to.
 * Returns null when name is not an import alias in the current module. */
const oak_module_t* oak_compiler_module_for_alias(
    const oak_compiler_t* c, const char* name);

/* Look up an exported function on an imported module identified by alias.
 * Sets *out_mod when the alias resolves (even when the function is missing),
 * so callers can cite mod->dotted_name in error messages.
 * Returns null when alias is unknown or the function is not exported. */
const oak_module_export_fn_t*
oak_compiler_module_export_fn(const oak_compiler_t* c,
                              const char* alias,
                              const char* fn_name,
                              const oak_module_t** out_mod);

/* Pattern detector: does `node` have the shape `IDENT.IDENT` where the lhs
 * IDENT is a known import alias?  If so, returns the resolved module and
 * writes the rhs token (e.g. the enum-type name in `color.Color`) to
 * *out_member.  Returns null when the pattern does not match. */
const oak_module_t*
oak_compiler_match_module_member(const oak_compiler_t* c,
                                 const oak_ast_node_t* node,
                                 const oak_token_t** out_member);

/* Like oak_compiler_module_export_fn but for record exports. */
const oak_module_export_record_t*
oak_compiler_module_export_record(const oak_compiler_t* c,
                                  const char* alias,
                                  const char* type_name,
                                  const oak_module_t** out_mod);

/*
 * Bring a dependency's type layout into this compilation on demand.
 *
 * `import m as a` binds the alias and imports nothing else, which is what an
 * alias is for. But a value obtained through it -- `a.make_point()` -- carries
 * a type id from m's registry, and field access, method dispatch and inference
 * all resolve a receiver through the *local* record registry. Without the
 * layout they see an unknown type and reject `.x` on a perfectly good record.
 *
 * So the layout is pulled across the moment a type actually crosses the alias,
 * rather than eagerly at import: only the types a program really uses arrive,
 * and a module aliased for one function does not drag its whole vocabulary
 * into the namespace.
 *
 * Both are no-ops for builtins and for a type already known locally, and both
 * are the machinery a selective import already uses for the types named in an
 * imported signature.
 */
void oak_ensure_dep_type_imported(oak_compiler_t* c,
                                  const oak_module_t* dep,
                                  const oak_type_t* type);

void oak_ensure_dep_named_type_imported(oak_compiler_t* c,
                                        const oak_module_t* dep,
                                        const char* name);

/* Like oak_compiler_module_export_fn but for enum exports. */
const oak_module_export_enum_t*
oak_compiler_module_export_enum(const oak_compiler_t* c,
                                const char* alias,
                                const char* enum_name,
                                const oak_module_t** out_mod);

/* Read at most `cap` IMPORT_PATH segments into out_segs[].
 * Returns the total number of segments (may exceed cap if the path is longer).
 * Used by record-literal compilation to split `Type` vs `mod.Type`. */
int
oak_compiler_import_path_segments(const oak_ast_node_t* path_node,
                                  const oak_ast_node_t** out_segs,
                                  int cap);
