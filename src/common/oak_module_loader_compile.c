#include "internal/oak_module_loader.h"
#include "internal/oak_lexer.h"

void loader_error(oak_module_loader_result_t* out,
                  const char* fmt,
                  ...)
{
  if (out->error_count >= OAK_MAX_DIAGNOSTICS)
    return;
  oak_diagnostic_t* d = &out->errors[out->error_count++];
  d->line = 0;
  d->column = 0;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d->message, sizeof(d->message), fmt, ap);
  va_end(ap);
}

void loader_propagate_diagnostics(oak_module_loader_result_t* out,
                                  const char* mod_label,
                                  const oak_diagnostic_t* src,
                                  int src_count)
{
  for (int i = 0; i < src_count && out->error_count < OAK_MAX_DIAGNOSTICS; ++i)
  {
    oak_diagnostic_t* d = &out->errors[out->error_count++];
    d->line = src[i].line;
    d->column = src[i].column;
    snprintf(d->message,
             sizeof(d->message),
             "%.64s: %.440s",
             mod_label ? mod_label : "<entry>",
             src[i].message);
  }
}

const oak_token_t* loader_import_alias_token(
    const loader_import_t* imp)
{
  if (imp->alias_node)
    return imp->alias_node->token;
  return OAK_NULL;
}

int collect_imports(const oak_module_t* mod, oak_container_t* out)
{
  const oak_ast_node_t* root = oak_parser_root(mod->parser);
  if (!root)
    return 0;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &root->children)
  {
    const oak_ast_node_t* item =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    loader_import_t imp = { 0 };
    if (item->kind == OAK_NODE_IMPORT_SELECTIVE)
    {
      imp.path = item->rhs;
      imp.alias_node = OAK_NULL;
    }
    else if (item->kind == OAK_NODE_IMPORT_WILDCARD)
    {
      imp.path = item->child;
      imp.alias_node = OAK_NULL;
    }
    else if (item->kind == OAK_NODE_IMPORT_DECL)
    {
      imp.path = item->lhs;
      imp.alias_node = item->rhs;
    }
    else
    {
      continue;
    }
    OAK_ASSERT(oak_push_back(out, &imp));
  }
  return (int)oak_size(out);
}

static int validate_imported_module_body(
    oak_module_loader_result_t* out,
    const oak_module_t* mod,
    const oak_ast_node_t* root)
{
  int ok = 1;
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &root->children)
  {
    const oak_ast_node_t* item =
        loader_unwrap_decl(OAK_CONTAINER_OF(pos, oak_ast_node_t, link));
    if (!item)
      continue;
    switch (item->kind)
    {
      case OAK_NODE_IMPORT_SELECTIVE:
      case OAK_NODE_IMPORT_WILDCARD:
      case OAK_NODE_IMPORT_DECL:
      case OAK_NODE_FN_DECL:
      case OAK_NODE_RECORD_DECL:
      case OAK_NODE_RECORD_DECL_EMPTY:
      case OAK_NODE_ENUM_DECL:
      case OAK_NODE_INTERFACE_DECL:
        continue;
      default:
        loader_error(out,
                     "%s: top-level statement not allowed in imported module "
                     "(only fn, record, enum, interface, and import are permitted)",
                     mod->dotted_name ? mod->dotted_name : mod->canonical_path);
        ok = 0;
        break;
    }
  }
  return ok;
}

int compile_module(oak_module_t* mod,
                   oak_compile_options_t* base_opts,
                   oak_module_registry_t* reg,
                   oak_module_loader_result_t* out)
{
  oak_compile_options_t opts = *base_opts;
  opts.source_name = mod->canonical_path;
  opts.module_registry = reg;
  opts.current_module = mod;
  opts.allow_bodyless_fns = opts_has_native_module(base_opts, mod->dotted_name);
  module_loader_filter_native_decls(base_opts, mod->dotted_name, &opts);

  if (!validate_bodyless_native_decls(out, mod, base_opts))
  {
    module_loader_free_filtered_native_decls(base_opts, mod->dotted_name, &opts);
    return -1;
  }

  oak_compile_result_t cr = { 0 };
  oak_compile_ex(oak_parser_root(mod->parser), &opts, &cr);

  if (cr.error_count > 0)
  {
    loader_propagate_diagnostics(
        out, mod->dotted_name, cr.errors, cr.error_count);
    oak_compile_result_free(&cr);
    module_loader_free_filtered_native_decls(base_opts, mod->dotted_name, &opts);
    return -1;
  }
  if (!cr.chunk)
  {
    loader_error(out, "%s: compilation produced no chunk", mod->dotted_name);
    module_loader_free_filtered_native_decls(base_opts, mod->dotted_name, &opts);
    return -1;
  }
  mod->chunk = cr.chunk;
  mod->state = OAK_MOD_COMPILED;
  apply_native_module_function_exports(mod, base_opts);
  module_loader_free_filtered_native_decls(base_opts, mod->dotted_name, &opts);
  return 0;
}

oak_module_t* parse_or_get_module(
    oak_module_registry_t* reg,
    const char* canonical_path,
    const char* dotted_name,
    int is_entry,
    oak_module_loader_result_t* out,
    int* created)
{
  oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical_path);
  if (existing)
  {
    *created = 0;
    return existing;
  }
  *created = 1;
  oak_module_t* mod =
      oak_module_registry_new(reg, canonical_path, dotted_name);
  if (!mod)
  {
    loader_error(out, "out of memory creating module '%s'", canonical_path);
    return OAK_NULL;
  }
  mod->is_entry = is_entry;

  if (oak_file_map(canonical_path, &mod->source) != 0)
  {
    loader_error(out, "could not open '%s'", canonical_path);
    return OAK_NULL;
  }
  mod->lexer =
      oak_lexer_tokenize_len(mod->source.data, mod->source.size, mod->allocator);
  mod->parser = oak_parse(mod->lexer, OAK_NODE_PROGRAM, mod->allocator);

  for (int i = 0; i < oak_parser_error_count(mod->parser) &&
                  out->error_count < OAK_MAX_DIAGNOSTICS;
       ++i)
  {
    const oak_diagnostic_t* d = &oak_parser_errors(mod->parser)[i];
    oak_diagnostic_t* dst = &out->errors[out->error_count++];
    dst->line = d->line;
    dst->column = d->column;
    snprintf(dst->message,
             sizeof(dst->message),
             "%.64s: %.440s",
             dotted_name,
             d->message);
  }

  const oak_ast_node_t* root = oak_parser_root(mod->parser);
  if (!root)
    return OAK_NULL;
  if (!is_entry && !validate_imported_module_body(out, mod, root))
    return OAK_NULL;

  return mod;
}
