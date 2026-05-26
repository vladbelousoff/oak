#pragma once

#include <oak_module_loader.h>

#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_dynarr.h"
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

/* ---------- Per-import descriptor ---------- */

struct loader_import_t
{
  const struct oak_ast_node_t* path;
  const struct oak_ast_node_t* alias_node;
};

struct loader_import_vec_t
{
  struct loader_import_t* items;
  int count;
  int capacity;
};

/* ---------- Diagnostics (oak_module_loader_compile.c) ---------- */

void loader_error(struct oak_module_loader_result_t* out,
                  const char* fmt,
                  ...);

void loader_propagate_diagnostics(struct oak_module_loader_result_t* out,
                                  const char* mod_label,
                                  const struct oak_diagnostic_t* src,
                                  int src_count);

/* ---------- Path helpers (oak_module_loader_path.c) ---------- */

char* path_dirname_dup(struct oak_allocator_t* a, const char* path);
char* path_resolve_dotted(struct oak_allocator_t* a, const char* base_dir, const char* dotted);
char* path_canonicalize(struct oak_allocator_t* a, const char* path);
int   path_exists(const char* path);
char* dotted_name_from_path(struct oak_allocator_t* a, const struct oak_ast_node_t* path_node);
const struct oak_ast_node_t* dotted_path_last_segment(
    const struct oak_ast_node_t* path_node);

/* ---------- AST helpers shared across loader translation units ---------- */

/* Strips an OAK_NODE_ATTR_DECL wrapper and returns the inner declaration.
 * Returns the node unchanged when it is not an attribute declaration. */
const struct oak_ast_node_t* loader_unwrap_decl(
    const struct oak_ast_node_t* item);

/* ---------- Native module helpers (oak_module_loader_native.c) ---------- */

int opts_has_native_module(const struct oak_compile_options_t* opts,
                           const char* dotted);

void module_loader_filter_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts);

void module_loader_free_filtered_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts);

void apply_native_module_function_exports(
    struct oak_module_t* mod,
    const struct oak_compile_options_t* opts);

int validate_bodyless_native_decls(struct oak_module_loader_result_t* out,
                                   const struct oak_module_t* mod,
                                   const struct oak_compile_options_t* opts);

struct oak_module_t* create_native_module(
    struct oak_module_registry_t* reg,
    const struct oak_compile_options_t* opts,
    const char* dotted,
    struct oak_module_loader_result_t* out);

/* ---------- Compile helpers (oak_module_loader_compile.c) ---------- */

const struct oak_token_t* loader_import_alias_token(
    const struct loader_import_t* imp);

int collect_imports(const struct oak_module_t* mod,
                    struct loader_import_vec_t* out);

struct oak_module_t* parse_or_get_module(
    struct oak_module_registry_t* reg,
    const char* canonical_path,
    const char* dotted_name,
    int is_entry,
    struct oak_module_loader_result_t* out,
    int* created);

int compile_module(struct oak_module_t* mod,
                   struct oak_compile_options_t* base_opts,
                   struct oak_module_registry_t* reg,
                   struct oak_module_loader_result_t* out);
