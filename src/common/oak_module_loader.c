#include "oak_module_loader.h"

#include "oak_bind.h"
#include "oak_chunk.h"
#include "oak_dynarr.h"
#include "oak_lexer.h"
#include "oak_log.h"
#include "oak_mem.h"

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

static void loader_error(struct oak_module_loader_result_t* out,
                         const char* fmt,
                         ...)
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

static void loader_propagate_diagnostics(
    struct oak_module_loader_result_t* out,
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
  const usize total = bdlen + 1u + dlen + 4u + 1u; /* sep + dotted + ".oak" + NUL */
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

static const struct oak_ast_node_t*
dotted_path_last_segment(const struct oak_ast_node_t* path_node)
{
  const struct oak_ast_node_t* last = null;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &path_node->children)
    last = oak_container_of(pos, struct oak_ast_node_t, link);
  return last;
}

/* ---------- Module body validation (non-entry) ---------- */

static int validate_imported_module_body(
    struct oak_module_loader_result_t* out,
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

  struct oak_compile_result_t cr = { 0 };
  oak_compile_ex(oak_parser_root(&mod->parser), &opts, &cr);

  if (cr.error_count > 0)
  {
    loader_propagate_diagnostics(
        out, mod->dotted_name, cr.errors, cr.error_count);
    oak_compile_result_free(&cr);
    return -1;
  }
  if (!cr.chunk)
  {
    loader_error(
        out, "%s: compilation produced no chunk", mod->dotted_name);
    return -1;
  }
  /* Ownership of the chunk transfers to the module. */
  mod->chunk = cr.chunk;
  mod->state = OAK_MOD_COMPILED;
  return 0;
}

/* ---------- DFS work item ---------- */

struct loader_frame_t
{
  struct oak_module_t* mod;
  /* Iterator over the module's import children — index of next import to
   * process. */
  int next_import_idx;
};

static struct oak_module_t* parse_or_get_module(
    struct oak_module_registry_t* reg,
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

  for (int i = 0;
       i < oak_parser_error_count(&mod->parser) &&
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

/* Counts top-level OAK_NODE_IMPORT_DECL items in a module's parse tree. */
static int count_imports(const struct oak_module_t* mod)
{
  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return 0;
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind == OAK_NODE_IMPORT_DECL)
      ++n;
  }
  return n;
}

/* Returns the i-th IMPORT_DECL child of the module's parsed program, or null
 * if out of range. */
static const struct oak_ast_node_t*
ith_import_decl(const struct oak_module_t* mod, int i)
{
  const struct oak_ast_node_t* root = oak_parser_root(&mod->parser);
  if (!root)
    return null;
  int n = 0;
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &root->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_IMPORT_DECL)
      continue;
    if (n == i)
      return item;
    ++n;
  }
  return null;
}

int oak_module_loader_load_program(
    const char* entry_path,
    struct oak_compile_options_t* opts,
    struct oak_module_registry_t* out_reg,
    struct oak_module_loader_result_t* out)
{
  out->entry = null;
  out->error_count = 0;

  /* Base directory for relative dotted-path resolution = directory of entry. */
  char* base_dir = path_dirname_dup(entry_path);
  char* entry_canonical = path_canonicalize(entry_path);

  /* Parse the entry module first. */
  int created = 0;
  struct oak_module_t* entry =
      parse_or_get_module(out_reg, entry_canonical, "<entry>", 1, out, &created);
  oak_free(entry_canonical, OAK_SRC_LOC);
  if (!entry || out->error_count > 0)
  {
    oak_free(base_dir, OAK_SRC_LOC);
    return -1;
  }

  /* DFS: explicit stack of frames so we can detect cycles via a "visiting"
   * flag (modules in `stack` are mid-processing). */
  OAK_DYNARR(struct loader_frame_t) stack;
  OAK_DYNARR_INIT(stack);
  /* topo: post-order list of modules, ready to compile. */
  OAK_DYNARR(struct oak_module_t*) topo;
  OAK_DYNARR_INIT(topo);
  /* visiting flags, indexed by module_id (resized as registry grows). */
  OAK_DYNARR(char) visiting;
  OAK_DYNARR_INIT(visiting);
  /* visited (post-order complete) flags. */
  OAK_DYNARR(char) visited;
  OAK_DYNARR_INIT(visited);

  #define ENSURE_FLAGS(_id) do {                                         \
    while (visiting.count <= (int)(_id)) OAK_DYNARR_PUSH(visiting, 0);   \
    while (visited.count  <= (int)(_id)) OAK_DYNARR_PUSH(visited,  0);   \
  } while (0)

  ENSURE_FLAGS(entry->module_id);
  visiting.items[entry->module_id] = 1;
  struct loader_frame_t entry_frame = { entry, 0 };
  OAK_DYNARR_PUSH(stack, entry_frame);

  int rc = 0;
  while (stack.count > 0 && rc == 0)
  {
    struct loader_frame_t* top = &stack.items[stack.count - 1];
    const int n_imports = count_imports(top->mod);
    if (top->next_import_idx >= n_imports)
    {
      /* All children processed: post-order finalize. */
      visiting.items[top->mod->module_id] = 0;
      visited.items[top->mod->module_id] = 1;
      OAK_DYNARR_PUSH(topo, top->mod);
      stack.count--;
      continue;
    }

    const struct oak_ast_node_t* import_decl =
        ith_import_decl(top->mod, top->next_import_idx);
    top->next_import_idx++;
    const struct oak_ast_node_t* path_node = import_decl->child;
    if (!path_node)
    {
      loader_error(out, "%s: malformed import", top->mod->dotted_name);
      rc = -1;
      break;
    }

    char* dotted = dotted_name_from_path(path_node);
    char* file_path = path_resolve_dotted(base_dir, dotted);
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
      /* Record the alias mapping on the parent module. */
      const struct oak_ast_node_t* last = dotted_path_last_segment(path_node);
      if (last)
      {
        const char* alias = oak_token_text(last->token);
        const usize alen = oak_token_length(last->token);
        if (oak_hash_table_get(&top->mod->imports, alias, alen) < 0)
          oak_hash_table_insert(
              &top->mod->imports, alias, alen, (int)found->module_id);
      }
      OAK_DYNARR_PUSH(top->mod->import_modules, found->module_id);
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      oak_free(canonical, OAK_SRC_LOC);
      continue;
    }

    /* Newly seen: parse + push frame. */
    int dep_created = 0;
    struct oak_module_t* dep = parse_or_get_module(
        out_reg, canonical, dotted, 0, out, &dep_created);
    if (!dep || out->error_count > 0)
    {
      oak_free(dotted, OAK_SRC_LOC);
      oak_free(file_path, OAK_SRC_LOC);
      oak_free(canonical, OAK_SRC_LOC);
      rc = -1;
      break;
    }

    /* Record the alias mapping on the parent module. */
    const struct oak_ast_node_t* last = dotted_path_last_segment(path_node);
    if (last)
    {
      const char* alias = oak_token_text(last->token);
      const usize alen = oak_token_length(last->token);
      if (oak_hash_table_get(&top->mod->imports, alias, alen) >= 0)
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
      oak_hash_table_insert(
          &top->mod->imports, alias, alen, (int)dep->module_id);
    }
    OAK_DYNARR_PUSH(top->mod->import_modules, dep->module_id);

    oak_free(dotted, OAK_SRC_LOC);
    oak_free(file_path, OAK_SRC_LOC);
    oak_free(canonical, OAK_SRC_LOC);

    ENSURE_FLAGS(dep->module_id);
    if (visiting.items[dep->module_id] || visited.items[dep->module_id])
      continue;
    visiting.items[dep->module_id] = 1;
    struct loader_frame_t f = { dep, 0 };
    OAK_DYNARR_PUSH(stack, f);
  }

  oak_free(base_dir, OAK_SRC_LOC);
  OAK_DYNARR_FREE(stack);
  OAK_DYNARR_FREE(visiting);
  OAK_DYNARR_FREE(visited);

  if (rc != 0)
  {
    OAK_DYNARR_FREE(topo);
    return rc;
  }

  /* Compile in topological order: each compile sees its dependencies' fully
   * populated export tables. */
  for (int i = 0; i < topo.count && rc == 0; ++i)
  {
    if (compile_module(topo.items[i], opts, out_reg, out) != 0)
      rc = -1;
  }
  OAK_DYNARR_FREE(topo);

  if (rc == 0)
    out->entry = entry;
  return rc;

  #undef ENSURE_FLAGS
}
