#include "oak_module_loader.h"

#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_dynarr.h"
#include "oak_lexer.h"
#include "oak_log.h"
#include "oak_mem.h"
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

/* ---------- Diagnostics helpers ---------- */

static void
loader_error(struct oak_module_loader_result_t* out, const char* fmt, ...)
{
  if (out->error_count >= OAK_MAX_DIAGNOSTICS)
    return;
  struct oak_diagnostic_t* d = &out->errors[out->error_count++];
  d->line = 0;
  d->column = 0;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d->message, sizeof(d->message), fmt, ap);
  va_end(ap);
}

static void loader_propagate_diagnostics(struct oak_module_loader_result_t* out,
                                         const char* mod_label,
                                         const struct oak_diagnostic_t* src,
                                         int src_count)
{
  for (int i = 0; i < src_count && out->error_count < OAK_MAX_DIAGNOSTICS; ++i)
  {
    struct oak_diagnostic_t* d = &out->errors[out->error_count++];
    d->line = src[i].line;
    d->column = src[i].column;
    /* Truncate the prefix label to 64 bytes so a long message body still
     * fits without the compiler warning about format truncation. */
    snprintf(d->message,
             sizeof(d->message),
             "%.64s: %.440s",
             mod_label ? mod_label : "<entry>",
             src[i].message);
  }
}

/* ---------- Path utilities ---------- */

static char* path_dirname_dup(const char* path)
{
  /* Returns a freshly allocated copy of the directory portion (no trailing
   * separator).  Returns "." for paths with no separator. */
  const char* last = null;
  for (const char* p = path; *p; ++p)
  {
    if (*p == '/' || *p == '\\')
      last = p;
  }
  if (!last)
  {
    char* dot = oak_alloc(2u, OAK_SRC_LOC);
    dot[0] = '.';
    dot[1] = 0;
    return dot;
  }
  const usize n = (usize)(last - path);
  char* d = oak_alloc(n + 1u, OAK_SRC_LOC);
  memcpy(d, path, n);
  d[n] = 0;
  return d;
}

static char* path_resolve_dotted(const char* base_dir, const char* dotted)
{
  /* Build "<base_dir>/<seg1>/<seg2>/.../<segN>.oak" by replacing every '.'
   * in dotted with OAK_PATH_SEP. */
  const usize bdlen = strlen(base_dir);
  const usize dlen = strlen(dotted);
  const usize total =
      bdlen + 1u + dlen + 4u + 1u; /* sep + dotted + ".oak" + NUL */
  char* out = oak_alloc(total, OAK_SRC_LOC);
  usize w = 0;
  memcpy(out + w, base_dir, bdlen);
  w += bdlen;
  if (bdlen == 0u || (out[bdlen - 1u] != '/' && out[bdlen - 1u] != '\\'))
    out[w++] = OAK_PATH_SEP;
  for (usize i = 0; i < dlen; ++i)
    out[w++] = (dotted[i] == '.') ? OAK_PATH_SEP : dotted[i];
  memcpy(out + w, ".oak", 4u);
  w += 4u;
  out[w] = 0;
  return out;
}

static char* path_canonicalize(const char* path)
{
  /* Best-effort canonicalisation. POSIX uses realpath; Windows _fullpath.
   * If canonicalisation fails (e.g. file doesn't exist yet), return a
   * duplicate of the input so the caller can still emit a meaningful error. */
#if defined(_WIN32)
  char* abs = _fullpath(null, path, 0);
  if (abs)
  {
    /* Re-allocate with oak_alloc so the lifetime matches the rest of the
     * loader's bookkeeping. */
    const usize n = strlen(abs);
    char* copy = oak_alloc(n + 1u, OAK_SRC_LOC);
    memcpy(copy, abs, n + 1u);
    free(abs);
    return copy;
  }
#else
  char buf[PATH_MAX];
  if (realpath(path, buf))
  {
    const usize n = strlen(buf);
    char* copy = oak_alloc(n + 1u, OAK_SRC_LOC);
    memcpy(copy, buf, n + 1u);
    return copy;
  }
#endif
  const usize n = strlen(path);
  char* copy = oak_alloc(n + 1u, OAK_SRC_LOC);
  memcpy(copy, path, n + 1u);
  return copy;
}

static int path_exists(const char* path)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

/* ---------- Dotted-name extraction from IMPORT_PATH AST ---------- */

static char* dotted_name_from_path(const struct oak_ast_node_t* path_node)
{
  /* path_node->children holds a sequence of OAK_NODE_IDENT tokens. */
  usize total = 0;
  int count = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children)
  {
    const struct oak_ast_node_t* ident =
        oak_container_of(pos, struct oak_ast_node_t, link);
    total += oak_token_length(ident->token);
    ++count;
  }
  if (count == 0)
  {
    char* empty = oak_alloc(1u, OAK_SRC_LOC);
    empty[0] = 0;
    return empty;
  }
  total += (usize)(count - 1); /* dots */
  char* buf = oak_alloc(total + 1u, OAK_SRC_LOC);
  usize w = 0;
  int first = 1;
  oak_list_for_each(pos, &path_node->children)
  {
    const struct oak_ast_node_t* ident =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (!first)
      buf[w++] = '.';
    const usize len = oak_token_length(ident->token);
    memcpy(buf + w, oak_token_text(ident->token), len);
    w += len;
    first = 0;
  }
  buf[w] = 0;
  return buf;
}

static int native_module_name_eq(const char* module_name, const char* dotted)
{
  return module_name && dotted && strcmp(module_name, dotted) == 0;
}

static int opts_has_native_module(const struct oak_compile_options_t* opts,
                                  const char* dotted)
{
  if (!opts || !dotted)
    return 0;
  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (type &&
        native_module_name_eq(type->module_name, dotted))
      return 1;
  }
  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums.items[i];
    if (e && native_module_name_eq(e->module_name, dotted))
      return 1;
  }
  return 0;
}

static char* native_canonical_path_dup(const char* dotted)
{
  const char* prefix = "native:";
  const usize plen = strlen(prefix);
  const usize dlen = strlen(dotted);
  char* out = oak_alloc(plen + dlen + 1u, OAK_SRC_LOC);
  memcpy(out, prefix, plen);
  memcpy(out + plen, dotted, dlen);
  out[plen + dlen] = 0;
  return out;
}

static const char* builtin_type_name(const oak_type_id_t id)
{
  switch (id)
  {
    case OAK_TYPE_NUMBER:
      return "number";
    case OAK_TYPE_STRING:
      return "string";
    case OAK_TYPE_BOOL:
      return "bool";
    default:
      return "unknown";
  }
}

static int native_type_in_module(const struct oak_compile_options_t* opts,
                                 oak_type_id_t type_id,
                                 const char* dotted)
{
  if (!opts || !dotted)
    return 0;
  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (type && type->type_id == type_id &&
        native_module_name_eq(type->module_name, dotted))
      return 1;
  }
  return 0;
}

static void module_loader_filter_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(base_opts, dotted))
    return;

  oak_dynarr_init(
      &opts->native_types.items, &opts->native_types.count, &opts->native_types.capacity);
  oak_dynarr_init(
      &opts->native_fns.items, &opts->native_fns.count, &opts->native_fns.capacity);
  oak_dynarr_init(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);

  for (int i = 0; i < base_opts->native_types.count; ++i)
  {
    struct oak_bind_type_t* type = base_opts->native_types.items[i];
    if (type &&
        native_module_name_eq(type->module_name, dotted))
      continue;
    oak_dynarr_push(&opts->native_types.items,
                    &opts->native_types.count,
                    &opts->native_types.capacity,
                    &type,
                    sizeof(type));
  }

  for (int i = 0; i < base_opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &base_opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted))
      continue;
    if (fn->receiver_type_id != OAK_TYPE_VOID &&
        native_type_in_module(base_opts, fn->receiver_type_id, dotted))
      continue;
    oak_dynarr_push(&opts->native_fns.items,
                    &opts->native_fns.count,
                    &opts->native_fns.capacity,
                    fn,
                    sizeof(*fn));
  }

  for (int i = 0; i < base_opts->native_enums.count; ++i)
  {
    struct oak_bind_enum_t* e = base_opts->native_enums.items[i];
    if (e && native_module_name_eq(e->module_name, dotted))
      continue;
    oak_dynarr_push(&opts->native_enums.items,
                    &opts->native_enums.count,
                    &opts->native_enums.capacity,
                    &e,
                    sizeof(e));
  }
}

static void module_loader_free_filtered_native_decls(
    const struct oak_compile_options_t* base_opts,
    const char* dotted,
    struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(base_opts, dotted))
    return;
  oak_dynarr_free(
      &opts->native_types.items, &opts->native_types.count, &opts->native_types.capacity);
  oak_dynarr_free(
      &opts->native_fns.items, &opts->native_fns.count, &opts->native_fns.capacity);
  oak_dynarr_free(&opts->native_enums.items,
                  &opts->native_enums.count,
                  &opts->native_enums.capacity);
}

static void apply_native_module_function_exports(
    struct oak_module_t* mod,
    const struct oak_compile_options_t* opts)
{
  if (!mod || !mod->chunk || !opts || !opts_has_native_module(opts, mod->dotted_name))
    return;
  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind != OAK_BIND_FN_GLOBAL ||
        !native_module_name_eq(fn->module_name, mod->dotted_name))
      continue;
    const int eidx =
        oak_htable_get(&mod->exports_fn_by_name, fn->name, strlen(fn->name));
    if (eidx < 0)
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(fn->impl, fn->arity, fn->name);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    struct oak_module_export_fn_t* exp = &mod->exports_fn.items[eidx];
    exp->const_idx = const_idx;
    exp->arity = fn->arity;
    exp->return_type_node = null;
    exp->return_type_id = fn->return_type_id;
    exp->return_kind = (fn->return_shape == OAK_BIND_SHAPE_ARRAY)
                           ? OAK_TYPE_KIND_ARRAY
                           : OAK_TYPE_KIND_SCALAR;
  }
}

static const struct oak_ast_node_t*
loader_fn_decl_name_node(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* head = proto ? proto->lhs : null;
  return head ? head->rhs : null;
}

static const struct oak_ast_node_t*
loader_fn_decl_param_list(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* proto = decl ? decl->lhs : null;
  const struct oak_ast_node_t* tail = proto ? proto->rhs : null;
  return tail ? tail->lhs : null;
}

static int loader_fn_decl_has_self(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  return plist && plist->lhs;
}

static int loader_fn_decl_param_count(const struct oak_ast_node_t* decl)
{
  const struct oak_ast_node_t* plist = loader_fn_decl_param_list(decl);
  if (!plist || !plist->rhs)
    return 0;
  return (int)oak_list_length(&plist->rhs->children);
}

static int loader_fn_decl_is_bodyless(const struct oak_ast_node_t* decl)
{
  return decl && decl->rhs && decl->rhs->kind == OAK_NODE_FN_DECL_SEMICOLON;
}

static const struct oak_ast_node_t*
loader_record_decl_name_node(const struct oak_ast_node_t* record_decl)
{
  const struct oak_ast_node_t* name = record_decl ? record_decl->lhs : null;
  if (name && name->kind == OAK_NODE_TYPE_NAME)
  {
    const struct oak_list_entry_t* first = name->children.next;
    if (first == &name->children)
      return null;
    name = oak_container_of(first, struct oak_ast_node_t, link);
  }
  return (name && name->kind == OAK_NODE_IDENT) ? name : null;
}

static const struct oak_bind_type_t*
find_native_type_decl(const struct oak_compile_options_t* opts,
                      const char* dotted,
                      const char* name)
{
  for (int i = 0; opts && i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (type && native_module_name_eq(type->module_name, dotted) &&
        strcmp(type->name, name) == 0)
      return type;
  }
  return null;
}

static int native_global_fn_decl_exists(const struct oak_compile_options_t* opts,
                                        const char* dotted,
                                        const char* name,
                                        int arity)
{
  for (int i = 0; opts && i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind == OAK_BIND_FN_GLOBAL &&
        native_module_name_eq(fn->module_name, dotted) &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

static int native_method_decl_exists(const struct oak_compile_options_t* opts,
                                     const struct oak_bind_type_t* receiver,
                                     const char* name,
                                     int has_self,
                                     int arity)
{
  for (int i = 0; opts && receiver && i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    const enum oak_bind_fn_kind_t want_kind =
        has_self ? OAK_BIND_FN_INSTANCE_METHOD : OAK_BIND_FN_STATIC_METHOD;
    if (fn->kind == want_kind && fn->receiver_type_id == receiver->type_id &&
        strcmp(fn->name, name) == 0 && fn->arity == arity)
      return 1;
  }
  return 0;
}

static int validate_bodyless_native_decls(
    struct oak_module_loader_result_t* out,
    const struct oak_module_t* mod,
    const struct oak_compile_options_t* opts)
{
  if (!opts_has_native_module(opts, mod->dotted_name))
    return 1;
  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return 1;
  int ok = 1;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind == OAK_NODE_FN_DECL && loader_fn_decl_is_bodyless(item))
    {
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(item);
      const char* name = oak_token_text(name_node->token);
      const usize name_len = oak_token_length(name_node->token);
      const int arity = loader_fn_decl_param_count(item);
      if (!native_global_fn_decl_exists(opts, mod->dotted_name, name, arity))
      {
        loader_error(out,
                     "%s: bodyless function '%.*s' has no native binding",
                     mod->dotted_name,
                     (int)name_len,
                     name);
        ok = 0;
      }
      continue;
    }
    if (item->kind != OAK_NODE_RECORD_DECL || !item->rhs)
      continue;
    const struct oak_ast_node_t* record_name_node =
        loader_record_decl_name_node(item);
    if (!record_name_node)
      continue;
    const char* record_name = oak_token_text(record_name_node->token);
    const usize record_name_len = oak_token_length(record_name_node->token);
    const struct oak_bind_type_t* receiver =
        find_native_type_decl(opts, mod->dotted_name, record_name);
    struct oak_list_entry_t* mpos;
    oak_list_for_each(mpos, &item->rhs->children)
    {
      const struct oak_ast_node_t* member =
          oak_container_of(mpos, struct oak_ast_node_t, link);
      if (member->kind != OAK_NODE_FN_DECL || !loader_fn_decl_is_bodyless(member))
        continue;
      const struct oak_ast_node_t* name_node = loader_fn_decl_name_node(member);
      const char* name = oak_token_text(name_node->token);
      const usize name_len = oak_token_length(name_node->token);
      const int has_self = loader_fn_decl_has_self(member);
      const int arity = loader_fn_decl_param_count(member);
      if (!native_method_decl_exists(opts, receiver, name, has_self, arity))
      {
        loader_error(out,
                     "%s: bodyless method '%.*s.%.*s' has no native binding",
                     mod->dotted_name,
                     (int)record_name_len,
                     record_name,
                     (int)name_len,
                     name);
        ok = 0;
      }
    }
  }
  return ok;
}

static struct oak_module_t*
create_native_module(struct oak_module_registry_t* reg,
                     const struct oak_compile_options_t* opts,
                     const char* dotted,
                     struct oak_module_loader_result_t* out)
{
  char* canonical = native_canonical_path_dup(dotted);
  struct oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical);
  if (existing)
  {
    oak_free(canonical, OAK_SRC_LOC);
    return existing;
  }

  struct oak_module_t* mod =
      oak_module_registry_create(reg, canonical, dotted);
  oak_free(canonical, OAK_SRC_LOC);
  if (!mod)
  {
    loader_error(out, "out of memory creating native module '%s'", dotted);
    return null;
  }

  mod->chunk = oak_alloc(sizeof(struct oak_chunk_t), OAK_SRC_LOC);
  oak_chunk_init(mod->chunk);
  mod->chunk->module_id = mod->module_id;

  for (int i = 0; i < opts->native_fns.count; ++i)
  {
    const struct oak_bind_fn_t* fn = &opts->native_fns.items[i];
    if (fn->kind != OAK_BIND_FN_GLOBAL ||
        !native_module_name_eq(fn->module_name, dotted))
      continue;
    struct oak_obj_native_fn_t* native =
        oak_native_fn_new(fn->impl, fn->arity, fn->name);
    const u16 const_idx =
        (u16)oak_chunk_add_constant(mod->chunk, OAK_VALUE_OBJ(&native->obj));
    struct oak_module_export_fn_t exp = {
      .name = fn->name,
      .name_len = strlen(fn->name),
      .const_idx = const_idx,
      .arity = fn->arity,
      .return_type_node = null,
      .return_type_id = fn->return_type_id,
      .return_kind = (fn->return_shape == OAK_BIND_SHAPE_ARRAY)
                         ? OAK_TYPE_KIND_ARRAY
                         : OAK_TYPE_KIND_SCALAR,
    };
    const int idx = mod->exports_fn.count;
    oak_dynarr_push(&mod->exports_fn.items,
                    &mod->exports_fn.count,
                    &mod->exports_fn.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_fn_by_name, exp.name, exp.name_len, idx);
  }

  for (int i = 0; i < opts->native_types.count; ++i)
  {
    const struct oak_bind_type_t* type = opts->native_types.items[i];
    if (!type ||
        !native_module_name_eq(type->module_name, dotted))
      continue;
    struct oak_module_export_record_t exp = { 0 };
    exp.name = type->name;
    exp.name_len = type->name_len;
    exp.field_count = type->field_count > OAK_MODULE_MAX_RECORD_FIELDS
                          ? OAK_MODULE_MAX_RECORD_FIELDS
                          : type->field_count;
    for (int fi = 0; fi < exp.field_count; ++fi)
    {
      exp.fields[fi].name = type->fields[fi].name;
      exp.fields[fi].name_len = type->fields[fi].name_len;
      exp.fields[fi].type_name = builtin_type_name(type->fields[fi].field_type_id);
      exp.fields[fi].type_name_len = strlen(exp.fields[fi].type_name);
    }
    const int idx = mod->exports_record.count;
    oak_dynarr_push(&mod->exports_record.items,
                    &mod->exports_record.count,
                    &mod->exports_record.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(
        &mod->exports_record_by_name, exp.name, exp.name_len, idx);
  }

  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* e = opts->native_enums.items[i];
    if (!e || !native_module_name_eq(e->module_name, dotted))
      continue;
    struct oak_module_export_enum_t exp = { 0 };
    exp.name = e->name;
    exp.name_len = e->name_len;
    exp.variant_count = e->variant_count > OAK_MODULE_MAX_ENUM_VARIANTS
                            ? OAK_MODULE_MAX_ENUM_VARIANTS
                            : e->variant_count;
    for (int vi = 0; vi < exp.variant_count; ++vi)
    {
      exp.variants[vi].name = e->variants[vi].name;
      exp.variants[vi].name_len = e->variants[vi].name_len;
      exp.variants[vi].value = e->variants[vi].value;
    }
    const int idx = mod->exports_enum.count;
    oak_dynarr_push(&mod->exports_enum.items,
                    &mod->exports_enum.count,
                    &mod->exports_enum.capacity,
                    &exp,
                    sizeof(exp));
    oak_htable_insert(&mod->exports_enum_by_name, exp.name, exp.name_len, idx);
  }

  mod->state = OAK_MOD_COMPILED;
  return mod;
}

static const struct oak_ast_node_t*
dotted_path_last_segment(const struct oak_ast_node_t* path_node)
{
  const struct oak_ast_node_t* last = null;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children) last =
      oak_container_of(pos, struct oak_ast_node_t, link);
  return last;
}

/* ---------- Per-import descriptor ---------- */

/* Flat descriptor extracted from one OAK_NODE_IMPORT_DECL. */
struct loader_import_t
{
  const struct oak_ast_node_t* path; /* OAK_NODE_IMPORT_PATH (decl->lhs) */
  const struct oak_ast_node_t*
      alias_node; /* explicit alias IDENT (decl->rhs) or null */
};

struct loader_import_vec_t
{
  struct loader_import_t* items;
  int count;
  int capacity;
};

/* Return the token that should be used as the import alias — the explicit `as
 * X` identifier when given, otherwise the last segment of the dotted path. */
static const struct oak_token_t*
loader_import_alias_token(const struct loader_import_t* imp)
{
  if (imp->alias_node)
    return imp->alias_node->token;
  const struct oak_ast_node_t* last = dotted_path_last_segment(imp->path);
  return last ? last->token : null;
}

/* Collect all import descriptors from a module's parse tree into `out`.
 * Returns the number collected. */
static int collect_imports(const struct oak_module_t* mod,
                           struct loader_import_vec_t* out)
{
  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_IMPORT_DECL)
      continue;
    struct loader_import_t imp;
    imp.path = item->lhs;
    imp.alias_node =
        (item->rhs && item->rhs->kind == OAK_NODE_IDENT) ? item->rhs : null;
    oak_dynarr_push(
        &out->items, &out->count, &out->capacity, &imp, sizeof(imp));
  }
  return out->count;
}

/* ---------- Module body validation (non-entry) ---------- */

static int validate_imported_module_body(struct oak_module_loader_result_t* out,
                                         const struct oak_module_t* mod,
                                         const struct oak_ast_node_t* root)
{
  /* Imported modules may only contain declarations: import / fn / record /
   * enum.  Top-level statements would require Python-style module-init
   * semantics, which v1 deliberately omits. */
  int ok = 1;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    switch (item->kind)
    {
      case OAK_NODE_IMPORT_DECL:
      case OAK_NODE_FN_DECL:
      case OAK_NODE_RECORD_DECL:
      case OAK_NODE_ENUM_DECL:
        continue;
      default:
        loader_error(out,
                     "%s: top-level statement not allowed in imported module "
                     "(only fn, record, enum, and import are permitted)",
                     mod->dotted_name ? mod->dotted_name : mod->canonical_path);
        ok = 0;
        break;
    }
  }
  return ok;
}

/* ---------- Per-module compilation orchestration ---------- */

static int compile_module(struct oak_module_t* mod,
                          struct oak_compile_options_t* base_opts,
                          struct oak_module_registry_t* reg,
                          struct oak_module_loader_result_t* out)
{
  /* Use a per-module copy of options so we can override source_name and
   * pass the module/registry pointers without mutating the caller's struct. */
  struct oak_compile_options_t opts = *base_opts;
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

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(oak_parser_root(&mod->parser), &opts, &cr);

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
  /* Ownership of the chunk transfers to the module. */
  mod->chunk = cr.chunk;
  mod->state = OAK_MOD_COMPILED;
  apply_native_module_function_exports(mod, base_opts);
  module_loader_free_filtered_native_decls(base_opts, mod->dotted_name, &opts);
  return 0;
}

/* ---------- DFS work item ---------- */

struct loader_frame_t
{
  struct oak_module_t* mod;
  struct loader_import_vec_t imports; /* pre-collected from parse tree */
  int next_import_idx;                /* cursor into imports */
};

struct loader_frame_vec_t
{
  struct loader_frame_t* items;
  int count;
  int capacity;
};
struct char_vec_t
{
  char* items;
  int count;
  int capacity;
};

static struct oak_module_t*
parse_or_get_module(struct oak_module_registry_t* reg,
                    const char* canonical_path,
                    const char* dotted_name,
                    int is_entry,
                    struct oak_module_loader_result_t* out,
                    int* created)
{
  struct oak_module_t* existing =
      oak_module_registry_find_by_path(reg, canonical_path);
  if (existing)
  {
    *created = 0;
    return existing;
  }
  *created = 1;
  struct oak_module_t* mod =
      oak_module_registry_create(reg, canonical_path, dotted_name);
  if (!mod)
  {
    loader_error(out, "out of memory creating module '%s'", canonical_path);
    return null;
  }
  mod->is_entry = is_entry;

  if (oak_file_map(canonical_path, &mod->source) != 0)
  {
    loader_error(out, "could not open '%s'", canonical_path);
    return null;
  }
  mod->lexer = oak_lexer_tokenize(mod->source.data, mod->source.size);
  oak_parse(mod->lexer, OAK_NODE_PROGRAM, &mod->parser);

  for (int i = 0; i < oak_parser_error_count(&mod->parser) &&
                  out->error_count < OAK_MAX_DIAGNOSTICS;
       ++i)
  {
    const struct oak_diagnostic_t* d = &oak_parser_errors(&mod->parser)[i];
    struct oak_diagnostic_t* dst = &out->errors[out->error_count++];
    dst->line = d->line;
    dst->column = d->column;
    snprintf(dst->message,
             sizeof(dst->message),
             "%.64s: %.440s",
             dotted_name,
             d->message);
  }

  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return null;
  if (!is_entry && !validate_imported_module_body(out, mod, root))
    return null;

  return mod;
}

int oak_module_loader_load_program(const char* entry_path,
                                   struct oak_compile_options_t* opts,
                                   struct oak_module_registry_t* out_reg,
                                   struct oak_module_loader_result_t* out)
{
  out->entry = null;
  out->error_count = 0;

  char* entry_canonical = path_canonicalize(entry_path);

  /* Parse the entry module first. */
  int created = 0;
  struct oak_module_t* entry = parse_or_get_module(
      out_reg, entry_canonical, "<entry>", 1, out, &created);
  oak_free(entry_canonical, OAK_SRC_LOC);
  if (!entry || out->error_count > 0)
  {
    return -1;
  }

  /* DFS: explicit stack of frames so we can detect cycles via a "visiting"
   * flag (modules in `stack` are mid-processing). */
  struct loader_frame_vec_t stack;
  oak_dynarr_init(&stack.items, &stack.count, &stack.capacity);
  /* topo: post-order list of modules, ready to compile. */
  struct oak_module_ptr_vec_t topo;
  oak_dynarr_init(&topo.items, &topo.count, &topo.capacity);
  /* visiting flags, indexed by module_id (resized as registry grows). */
  struct char_vec_t visiting;
  oak_dynarr_init(&visiting.items, &visiting.count, &visiting.capacity);
  /* visited (post-order complete) flags. */
  struct char_vec_t visited;
  oak_dynarr_init(&visited.items, &visited.count, &visited.capacity);

#define ENSURE_FLAGS(_id)                                                      \
  do                                                                           \
  {                                                                            \
    while (visiting.count <= (int)(_id))                                       \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_dynarr_push(&visiting.items,                                         \
                      &visiting.count,                                         \
                      &visiting.capacity,                                      \
                      &_zz,                                                    \
                      sizeof(char));                                           \
    }                                                                          \
    while (visited.count <= (int)(_id))                                        \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_dynarr_push(&visited.items,                                          \
                      &visited.count,                                          \
                      &visited.capacity,                                       \
                      &_zz,                                                    \
                      sizeof(char));                                           \
    }                                                                          \
  } while (0)

  ENSURE_FLAGS(entry->module_id);
  visiting.items[entry->module_id] = 1;
  {
    struct loader_frame_t entry_frame;
    entry_frame.mod = entry;
    oak_dynarr_init(&entry_frame.imports.items,
                    &entry_frame.imports.count,
                    &entry_frame.imports.capacity);
    collect_imports(entry, &entry_frame.imports);
    entry_frame.next_import_idx = 0;
    oak_dynarr_push(&stack.items,
                    &stack.count,
                    &stack.capacity,
                    &entry_frame,
                    sizeof(entry_frame));
  }

/* Helper: record the alias→module_id mapping from a loader_import_t. */
#define RECORD_ALIAS(_parent_mod, _imp, _dep_id)                               \
  do                                                                           \
  {                                                                            \
    const struct oak_token_t* _atk = loader_import_alias_token(_imp);          \
    if (_atk)                                                                  \
    {                                                                          \
      const char* _a = oak_token_text(_atk);                                   \
      const usize _al = oak_token_length(_atk);                                \
      if (oak_htable_get(&(_parent_mod)->imports, _a, _al) < 0)                \
        oak_htable_insert(&(_parent_mod)->imports, _a, _al, (int)(_dep_id));   \
    }                                                                          \
  } while (0)

  int rc = 0;
  while (stack.count > 0 && rc == 0)
  {
    struct loader_frame_t* top = &stack.items[stack.count - 1];
    if (top->next_import_idx >= top->imports.count)
    {
      /* All children processed: post-order finalize. */
      visiting.items[top->mod->module_id] = 0;
      visited.items[top->mod->module_id] = 1;
      oak_dynarr_push(&topo.items,
                      &topo.count,
                      &topo.capacity,
                      &top->mod,
                      sizeof(top->mod));
      oak_dynarr_free(
          &top->imports.items, &top->imports.count, &top->imports.capacity);
      stack.count--;
      continue;
    }

    const struct loader_import_t* imp =
        &top->imports.items[top->next_import_idx++];
    if (!imp->path)
    {
      loader_error(out, "%s: malformed import", top->mod->dotted_name);
      rc = -1;
      break;
    }

    char* dotted = dotted_name_from_path(imp->path);

    char* mod_dir = path_dirname_dup(top->mod->canonical_path);
    char* file_path = path_resolve_dotted(mod_dir, dotted);
    oak_free(mod_dir, OAK_SRC_LOC);

    if (!path_exists(file_path) && opts_has_native_module(opts, dotted))
    {
      oak_free(file_path, OAK_SRC_LOC);
      file_path = path_resolve_dotted("stdlib", dotted);
#if defined(OAK_STDLIB_DIR)
      if (!path_exists(file_path))
      {
        oak_free(file_path, OAK_SRC_LOC);
        file_path = path_resolve_dotted(OAK_STDLIB_DIR, dotted);
      }
#endif
    }

    if (!path_exists(file_path) && opts_has_native_module(opts, dotted))
    {
      struct oak_module_t* dep =
          create_native_module(out_reg, opts, dotted, out);
      if (!dep || out->error_count > 0)
      {
        oak_free(dotted, OAK_SRC_LOC);
        rc = -1;
        break;
      }
      const struct oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const usize alen = oak_token_length(atk);
        if (oak_htable_get(&top->mod->imports, alias, alen) >= 0)
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                       (int)alen,
                       alias);
          oak_free(dotted, OAK_SRC_LOC);
          rc = -1;
          break;
        }
        oak_htable_insert(&top->mod->imports, alias, alen, (int)dep->module_id);
      }
      oak_dynarr_push(&top->mod->import_modules.items,
                      &top->mod->import_modules.count,
                      &top->mod->import_modules.capacity,
                      &dep->module_id,
                      sizeof(dep->module_id));
      ENSURE_FLAGS(dep->module_id);
      visited.items[dep->module_id] = 1;
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      continue;
    }

    char* canonical = path_canonicalize(file_path);

    /* Cycle check: is `canonical` currently being visited? */
    struct oak_module_t* found =
        oak_module_registry_find_by_path(out_reg, canonical);
    if (found && (int)found->module_id < visiting.count &&
        visiting.items[found->module_id])
    {
      /* Build a path string from the visit stack: a -> b -> ... -> a. */
      char buf[256];
      usize w = 0;
      for (int i = 0; i < stack.count && w < sizeof(buf) - 8; ++i)
      {
        const char* nm = stack.items[i].mod->dotted_name;
        const usize nl = strlen(nm);
        if (w + nl + 4 >= sizeof(buf))
          break;
        memcpy(buf + w, nm, nl);
        w += nl;
        memcpy(buf + w, " -> ", 4);
        w += 4;
      }
      const usize dl = strlen(dotted);
      if (w + dl < sizeof(buf))
      {
        memcpy(buf + w, dotted, dl);
        w += dl;
      }
      buf[w] = 0;
      loader_error(out, "import cycle: %s", buf);
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      oak_free(canonical, OAK_SRC_LOC);
      rc = -1;
      break;
    }

    /* Already-visited modules: just record edge, don't recurse. */
    if (found && (int)found->module_id < visited.count &&
        visited.items[found->module_id])
    {
      RECORD_ALIAS(top->mod, imp, found->module_id);
      oak_dynarr_push(&top->mod->import_modules.items,
                      &top->mod->import_modules.count,
                      &top->mod->import_modules.capacity,
                      &found->module_id,
                      sizeof(found->module_id));
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      oak_free(canonical, OAK_SRC_LOC);
      continue;
    }

    /* Newly seen: parse + push frame. */
    int dep_created = 0;
    struct oak_module_t* dep =
        parse_or_get_module(out_reg, canonical, dotted, 0, out, &dep_created);
    if (!dep || out->error_count > 0)
    {
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      oak_free(canonical, OAK_SRC_LOC);
      rc = -1;
      break;
    }

    /* Validate and record the alias. */
    {
      const struct oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const usize alen = oak_token_length(atk);
        if (oak_htable_get(&top->mod->imports, alias, alen) >= 0)
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                          (int)alen,
                          alias);
          oak_free(dotted, OAK_SRC_LOC);
          oak_free(file_path, OAK_SRC_LOC);
          oak_free(canonical, OAK_SRC_LOC);
          rc = -1;
          break;
        }
        oak_htable_insert(&top->mod->imports, alias, alen, (int)dep->module_id);
      }
    }
    oak_dynarr_push(&top->mod->import_modules.items,
                    &top->mod->import_modules.count,
                    &top->mod->import_modules.capacity,
                    &dep->module_id,
                    sizeof(dep->module_id));

    oak_free(dotted, OAK_SRC_LOC);
    oak_free(file_path, OAK_SRC_LOC);
    oak_free(canonical, OAK_SRC_LOC);

    ENSURE_FLAGS(dep->module_id);
    if (visiting.items[dep->module_id] || visited.items[dep->module_id])
      continue;
    visiting.items[dep->module_id] = 1;
    {
      struct loader_frame_t f;
      f.mod = dep;
      oak_dynarr_init(&f.imports.items, &f.imports.count, &f.imports.capacity);
      collect_imports(dep, &f.imports);
      f.next_import_idx = 0;
      oak_dynarr_push(
          &stack.items, &stack.count, &stack.capacity, &f, sizeof(f));
    }
  }

  /* Free any imports arrays still on the stack (error path). */
  for (int i = 0; i < stack.count; ++i)
    oak_dynarr_free(&stack.items[i].imports.items,
                    &stack.items[i].imports.count,
                    &stack.items[i].imports.capacity);

  oak_dynarr_free(&stack.items, &stack.count, &stack.capacity);
  oak_dynarr_free(&visiting.items, &visiting.count, &visiting.capacity);
  oak_dynarr_free(&visited.items, &visited.count, &visited.capacity);

#undef RECORD_ALIAS

  if (rc != 0)
  {
    oak_dynarr_free(&topo.items, &topo.count, &topo.capacity);
    return rc;
  }

  /* Compile in topological order: each compile sees its dependencies' fully
   * populated export tables. */
  for (int i = 0; i < topo.count && rc == 0; ++i)
  {
    if (compile_module(topo.items[i], opts, out_reg, out) != 0)
      rc = -1;
  }
  oak_dynarr_free(&topo.items, &topo.count, &topo.capacity);

  if (rc == 0)
    out->entry = entry;
  return rc;

#undef ENSURE_FLAGS
}
