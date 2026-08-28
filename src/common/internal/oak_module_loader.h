#pragma once

#include <oak_module_loader.h>

#include "oak_bind.h"
#include "oak_chunk_impl.h"
#include "oak_container.h"
#include "oak_module_impl.h"
#include "oak_lexer.h"
#include "oak_allocator.h"
#include "oak_log.h"
#include "oak_str.h"
#include "oak_type.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <stdlib.h>
#define OAK_PATH_SEP '\\'
#else
#include <limits.h>
#include <stdlib.h>
#define OAK_PATH_SEP '/'
#endif


typedef struct loader_import loader_import_t;
struct loader_import
{
  const oak_ast_node_t* path;
  const oak_ast_node_t* alias_node;
};



void loader_error(oak_module_loader_result_t* out,
                  const char* fmt,
                  ...);

void loader_propagate_diagnostics(oak_module_loader_result_t* out,
                                  const char* mod_label,
                                  const oak_diagnostic_t* src,
                                  int src_count);


char* path_dirname_dup(oak_allocator_t* a, const char* path);
char* path_resolve_dotted(oak_allocator_t* a, const char* base_dir, const char* dotted);
char* path_join(oak_allocator_t* a, const char* base, const char* rel);
char* path_canonicalize(oak_allocator_t* a, const char* path);
int   path_exists(const char* path);
int   path_dir_exists(const char* path);
/* Directory containing the running executable, or null if it cannot be
 * determined. Caller owns the returned string. */
char* path_executable_dir(oak_allocator_t* a);
char* dotted_name_from_path(oak_allocator_t* a, const oak_ast_node_t* path_node);


/* Strips OAK_NODE_ATTR_DECL / OAK_NODE_EXPORT_DECL wrappers and returns the
 * inner declaration. Returns the node unchanged when it has no wrapper. */
const oak_ast_node_t* loader_unwrap_decl(
    const oak_ast_node_t* item);


int opts_has_native_module(const oak_compile_options_t* opts,
                           const char* dotted);

void module_loader_filter_native_decls(
    const oak_compile_options_t* base_opts,
    const char* dotted,
    oak_compile_options_t* opts);

void module_loader_free_filtered_native_decls(
    const oak_compile_options_t* base_opts,
    const char* dotted,
    oak_compile_options_t* opts);

void apply_native_module_function_exports(
    oak_module_t* mod,
    const oak_compile_options_t* opts);

int validate_bodyless_native_decls(oak_module_loader_result_t* out,
                                   const oak_module_t* mod,
                                   const oak_compile_options_t* opts);

oak_module_t* create_native_module(
    oak_module_registry_t* reg,
    const oak_compile_options_t* opts,
    const char* dotted,
    oak_module_loader_result_t* out);


const oak_token_t* loader_import_alias_token(
    const loader_import_t* imp);

/* Appends loader_import_t entries to `out`; returns the total count. */
int collect_imports(const oak_module_t* mod, oak_container_t* out);

oak_module_t* parse_or_get_module(
    oak_module_registry_t* reg,
    const char* canonical_path,
    const char* dotted_name,
    int is_entry,
    oak_module_loader_result_t* out,
    int* created);

int compile_module(oak_module_t* mod,
                   oak_compile_options_t* base_opts,
                   oak_module_registry_t* reg,
                   oak_module_loader_result_t* out);
