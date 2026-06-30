#include "internal/oak_module_loader.h"

/* ---------- DFS work structures ---------- */

struct loader_frame_t
{
  struct oak_module_t* mod;
  struct loader_import_t* imports;
  int next_import_idx;
};

/* Where stdlib_resolve found (or committed to) a base directory. Anything other
 * than FALLBACK is authoritative: on a miss the caller must report an error
 * rather than degrade to a synthetic native module or a different stdlib. */
enum stdlib_origin_t
{
  STDLIB_ORIGIN_FALLBACK = 0, /* baked source dir / CWD; miss may be native */
  STDLIB_ORIGIN_OVERRIDE,     /* $OAK_STDLIB_DIR */
  STDLIB_ORIGIN_INSTALL,      /* executable-relative installed layout */
};

/* Resolve a dotted stdlib module name (e.g. "io") to a file path, searching a
 * series of candidate base directories in priority order:
 *
 *   1. $OAK_STDLIB_DIR              - authoritative override (no fallback)
 *   2. <exe-dir>/INSTALL_RELPATH    - installed layout; authoritative once found
 *   3. OAK_STDLIB_SOURCE_DIR        - source tree path baked in at build time
 *   4. ./stdlib                     - current-directory fallback (last resort)
 *
 * The install location is found RELATIVE TO THE EXECUTABLE (e.g.
 * <prefix>/bin/oak -> <prefix>/share/oak/stdlib), not via a baked absolute path.
 * This makes an installed tree relocatable and distinguishes the two kinds of
 * binary without a runtime flag. When the executable-relative stdlib *directory*
 * exists, the binary is treated as installed and that directory is
 * authoritative: a missing module is an error rather than a silent fall-through
 * to the build source tree (which would not exist after packaging, masking a
 * broken install). A build-tree binary has no such co-located directory, so it
 * skips to OAK_STDLIB_SOURCE_DIR and never consults any install prefix, so a
 * stale prior install cannot contaminate development or test runs. The
 * CWD-relative path is searched LAST so it can never shadow a real stdlib.
 *
 * Limitation: build-tree and installed binaries are byte-identical, so an
 * installed binary can only recognize itself as installed by the presence of its
 * co-located stdlib directory. If a package omits that directory entirely, the
 * binary cannot distinguish itself from a build-tree run and falls back. The
 * common breakage (the directory present but a module missing) is still caught
 * as an authoritative error below.
 *
 * *origin reports the authority of the chosen base (see enum). Returns the first
 * existing candidate, or the chosen authoritative / last-resort path so the
 * caller's error reporting stays sensible. The returned string is owned by the
 * caller. */
static char* stdlib_resolve(struct oak_allocator_t* a, const char* dotted,
                            enum stdlib_origin_t* origin)
{
  *origin = STDLIB_ORIGIN_FALLBACK;

  const char* env_dir = getenv("OAK_STDLIB_DIR");
  if (env_dir && env_dir[0])
  {
    *origin = STDLIB_ORIGIN_OVERRIDE;
    return path_resolve_dotted(a, env_dir, dotted);
  }

#if defined(OAK_STDLIB_INSTALL_RELPATH)
  {
    char* exe_dir = path_executable_dir(a);
    if (exe_dir)
    {
      char* base = path_join(a, exe_dir, OAK_STDLIB_INSTALL_RELPATH);
      OAK_FREE(a, exe_dir);
      if (path_dir_exists(base))
      {
        *origin = STDLIB_ORIGIN_INSTALL;
        char* p = path_resolve_dotted(a, base, dotted);
        OAK_FREE(a, base);
        return p;
      }
      OAK_FREE(a, base);
    }
  }
#endif

#if defined(OAK_STDLIB_SOURCE_DIR)
  {
    char* p = path_resolve_dotted(a, OAK_STDLIB_SOURCE_DIR, dotted);
    if (path_exists(p))
      return p;
    OAK_FREE(a, p);
  }
#endif

  return path_resolve_dotted(a, "stdlib", dotted);
}


int oak_module_loader_load_program(const char* entry_path,
                                   struct oak_compile_options_t* opts,
                                   struct oak_module_registry_t* out_reg,
                                   struct oak_module_loader_result_t* out)
{
  out->entry = null;
  out->error_count = 0;
  struct oak_allocator_t* a = out_reg->allocator;

  char* entry_canonical = path_canonicalize(a, entry_path);

  int created = 0;
  struct oak_module_t* entry = parse_or_get_module(
      out_reg, entry_canonical, "<entry>", 1, out, &created);
  OAK_FREE(a, entry_canonical);
  if (!entry || out->error_count > 0)
    return -1;

  struct loader_frame_t* stack;
  oak_assert(oak_dynarr_init(a, &stack, sizeof *stack));
  struct oak_module_t** topo;
  oak_assert(oak_dynarr_init(a, &topo, sizeof *topo));
  char* visiting;
  oak_assert(oak_dynarr_init(a, &visiting, sizeof *visiting));
  char* visited;
  oak_assert(oak_dynarr_init(a, &visited, sizeof *visited));

#define ENSURE_FLAGS(_id)                                                      \
  do                                                                           \
  {                                                                            \
    while (oak_dynarr_count(visiting) <= (int)(_id))                           \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_assert(oak_dynarr_push(&visiting, &_zz));                            \
    }                                                                          \
    while (oak_dynarr_count(visited) <= (int)(_id))                            \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_assert(oak_dynarr_push(&visited, &_zz));                             \
    }                                                                          \
  } while (0)

  ENSURE_FLAGS(entry->module_id);
  visiting[entry->module_id] = 1;
  {
    struct loader_frame_t entry_frame;
    entry_frame.mod = entry;
    oak_assert(
        oak_dynarr_init(a, &entry_frame.imports, sizeof *entry_frame.imports));
    collect_imports(entry, &entry_frame.imports);
    entry_frame.next_import_idx = 0;
    oak_assert(oak_dynarr_push(&stack, &entry_frame));
  }

#define RECORD_ALIAS(_parent_mod, _imp, _dep_id)                               \
  do                                                                           \
  {                                                                            \
    const struct oak_token_t* _atk = loader_import_alias_token(_imp);          \
    if (_atk)                                                                  \
    {                                                                          \
      const char* _a = oak_token_text(_atk);                                   \
      const int _al = oak_token_size(_atk);                                \
      if (oak_htable_get(&(_parent_mod)->imports, _a, _al) < 0)                \
        oak_htable_insert(&(_parent_mod)->imports, _a, _al, (int)(_dep_id));   \
    }                                                                          \
  } while (0)

  int rc = 0;
  while (oak_dynarr_count(stack) > 0 && rc == 0)
  {
    struct loader_frame_t* top = &stack[oak_dynarr_count(stack) - 1];
    if (top->next_import_idx >= oak_dynarr_count(top->imports))
    {
      visiting[top->mod->module_id] = 0;
      visited[top->mod->module_id] = 1;
      oak_assert(oak_dynarr_push(&topo, &top->mod));
      oak_dynarr_free(&top->imports);
      oak_assert(oak_dynarr_pop(&stack, null));
      continue;
    }

    const struct loader_import_t* imp =
        &top->imports[top->next_import_idx++];
    if (!imp->path)
    {
      loader_error(out, "%s: malformed import", top->mod->dotted_name);
      rc = -1;
      break;
    }

    char* dotted = dotted_name_from_path(a, imp->path);
    const int is_native = opts_has_native_module(opts, dotted);

    char* file_path = null;
    if (is_native)
    {
      /* Native stdlib modules (e.g. io) resolve against the stdlib BEFORE any
       * module-relative file, so a project-local <dir>/io.oak cannot shadow the
       * trusted stdlib and an authoritative $OAK_STDLIB_DIR / installed stdlib
       * really is authoritative. A miss from an authoritative source is a hard
       * error: report the path rather than degrading to a synthetic native
       * module (which would silently drop stub-only declarations) or to a
       * different stdlib. */
      enum stdlib_origin_t origin = STDLIB_ORIGIN_FALLBACK;
      file_path = stdlib_resolve(a, dotted, &origin);
      if (origin != STDLIB_ORIGIN_FALLBACK && !path_exists(file_path))
      {
        loader_error(out,
                     "cannot find stdlib module '%s' in %s: %s",
                     dotted,
                     origin == STDLIB_ORIGIN_OVERRIDE
                         ? "OAK_STDLIB_DIR"
                         : "the installed stdlib",
                     file_path);
        OAK_FREE(a, file_path);
        OAK_FREE(a, dotted);
        rc = -1;
        break;
      }
    }
    else
    {
      char* mod_dir = path_dirname_dup(a, top->mod->canonical_path);
      file_path = path_resolve_dotted(a, mod_dir, dotted);
      OAK_FREE(a, mod_dir);
    }

    if (!path_exists(file_path) && is_native)
    {
      struct oak_module_t* dep =
          create_native_module(out_reg, opts, dotted, out);
      if (!dep || out->error_count > 0)
      {
        OAK_FREE(a, dotted);
        rc = -1;
        break;
      }
      const struct oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const int alen = oak_token_size(atk);
        if (oak_htable_get(&top->mod->imports, alias, alen) >= 0)
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                       (int)alen,
                       alias);
          OAK_FREE(a, dotted);
          rc = -1;
          break;
        }
        oak_htable_insert(&top->mod->imports, alias, alen, (int)dep->module_id);
      }
      oak_assert(oak_dynarr_push(&top->mod->import_modules, &dep->module_id));
      ENSURE_FLAGS(dep->module_id);
      visited[dep->module_id] = 1;
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      continue;
    }

    char* canonical = path_canonicalize(a, file_path);

    struct oak_module_t* found =
        oak_module_registry_find_by_path(out_reg, canonical);
    if (found && (int)found->module_id < oak_dynarr_count(visiting) &&
        visiting[found->module_id])
    {
      char buf[256];
      usize w = 0;
      for (int i = 0; i < oak_dynarr_count(stack) && w < sizeof(buf) - 8; ++i)
      {
        const char* nm = stack[i].mod->dotted_name;
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
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      OAK_FREE(a, canonical);
      rc = -1;
      break;
    }

    if (found && (int)found->module_id < oak_dynarr_count(visited) &&
        visited[found->module_id])
    {
      RECORD_ALIAS(top->mod, imp, found->module_id);
      oak_assert(oak_dynarr_push(&top->mod->import_modules, &found->module_id));
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      OAK_FREE(a, canonical);
      continue;
    }

    int dep_created = 0;
    struct oak_module_t* dep =
        parse_or_get_module(out_reg, canonical, dotted, 0, out, &dep_created);
    if (!dep || out->error_count > 0)
    {
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      OAK_FREE(a, canonical);
      rc = -1;
      break;
    }

    {
      const struct oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const int alen = oak_token_size(atk);
        if (oak_htable_get(&top->mod->imports, alias, alen) >= 0)
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                       (int)alen,
                       alias);
          OAK_FREE(a, dotted);
          OAK_FREE(a, file_path);
          OAK_FREE(a, canonical);
          rc = -1;
          break;
        }
        oak_htable_insert(&top->mod->imports, alias, alen, (int)dep->module_id);
      }
    }
    oak_assert(oak_dynarr_push(&top->mod->import_modules, &dep->module_id));

    OAK_FREE(a, dotted);
    OAK_FREE(a, file_path);
    OAK_FREE(a, canonical);

    ENSURE_FLAGS(dep->module_id);
    if (visiting[dep->module_id] || visited[dep->module_id])
      continue;
    visiting[dep->module_id] = 1;
    {
      struct loader_frame_t f;
      f.mod = dep;
      oak_assert(oak_dynarr_init(a, &f.imports, sizeof *f.imports));
      collect_imports(dep, &f.imports);
      f.next_import_idx = 0;
      oak_assert(oak_dynarr_push(&stack, &f));
    }
  }

  for (int i = 0; i < oak_dynarr_count(stack); ++i)
    oak_dynarr_free(&stack[i].imports);

  oak_dynarr_free(&stack);
  oak_dynarr_free(&visiting);
  oak_dynarr_free(&visited);

#undef RECORD_ALIAS

  if (rc != 0)
  {
    oak_dynarr_free(&topo);
    return rc;
  }

  for (int i = 0; i < oak_dynarr_count(topo) && rc == 0; ++i)
  {
    if (compile_module(topo[i], opts, out_reg, out) != 0)
      rc = -1;
  }
  oak_dynarr_free(&topo);

  if (rc == 0)
    out->entry = entry;
  return rc;

#undef ENSURE_FLAGS
}
