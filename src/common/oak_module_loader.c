#include "internal/oak_module_loader.h"

#include "oak_module_mount.h"


typedef struct loader_frame loader_frame_t;
struct loader_frame
{
  oak_module_t* mod;
  /* loader_import_t */
  oak_container_t* imports;
  usize next_import_idx;
};

/* Where stdlib_resolve found (or committed to) a base directory. Anything other
 * than FALLBACK is authoritative: on a miss the caller must report an error
 * rather than degrade to a synthetic native module or a different stdlib. */
typedef enum stdlib_origin stdlib_origin_t;
enum stdlib_origin
{
  STDLIB_ORIGIN_FALLBACK = 0, /* baked source dir / CWD; miss may be native */
  STDLIB_ORIGIN_OVERRIDE,     /* $OAK_STDLIB_DIR */
  STDLIB_ORIGIN_INSTALL,      /* executable-relative installed layout */
};

/* Resolve a dotted stdlib module name (e.g. "io") to a file path, searching a
 * series of candidate base directories in priority order:
 *
 *   1. $OAK_STDLIB_DIR              - authoritative override (no fallback)
 *   2. <exe-dir>/stdlib             - installed layout; authoritative once found
 *   3. OAK_STDLIB_SOURCE_DIR        - source tree path baked in at build time
 *   4. ./stdlib                     - current-directory fallback (last resort)
 *
 * The install location is found RELATIVE TO THE EXECUTABLE (e.g.
 * <prefix>/bin/oak -> <prefix>/bin/stdlib), not via a baked absolute path. This
 * makes an installed tree relocatable and distinguishes the two kinds of binary
 * without a runtime flag. When the executable-relative stdlib *directory* exists,
 * the binary is treated as installed and that directory is
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
static char* stdlib_resolve(oak_allocator_t* a, const char* dotted,
                            stdlib_origin_t* origin)
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
      oak_free(a, exe_dir, OAK_HERE);
      if (path_dir_exists(base))
      {
        *origin = STDLIB_ORIGIN_INSTALL;
        char* p = path_resolve_dotted(a, base, dotted);
        oak_free(a, base, OAK_HERE);
        return p;
      }
      oak_free(a, base, OAK_HERE);
    }
  }
#endif

#if defined(OAK_STDLIB_SOURCE_DIR)
  {
    char* p = path_resolve_dotted(a, OAK_STDLIB_SOURCE_DIR, dotted);
    if (path_exists(p))
      return p;
    oak_free(a, p, OAK_HERE);
  }
#endif

  return path_resolve_dotted(a, "stdlib", dotted);
}


int oak_module_loader_load_program(const char* entry_path,
                                   oak_compile_options_t* opts,
                                   oak_module_registry_t* out_reg,
                                   oak_module_loader_result_t* out)
{
  out->entry = OAK_NULL;
  out->error_count = 0;
  oak_allocator_t* a = out_reg->allocator;

  char* entry_canonical = path_canonicalize(a, entry_path);

  int created = 0;
  oak_module_t* entry = parse_or_get_module(
      out_reg, entry_canonical, "<entry>", 1, out, &created);
  oak_free(a, entry_canonical, OAK_HERE);
  if (!entry || out->error_count > 0)
    return -1;

  oak_container_t* stack = oak_vector_new(a, sizeof(loader_frame_t));
  oak_container_t* topo = oak_vector_new(a, sizeof(oak_module_t*));
  /* Per-module-id DFS colours, indexed by module_id. */
  oak_container_t* visiting = oak_vector_new(a, sizeof(char));
  oak_container_t* visited = oak_vector_new(a, sizeof(char));
  OAK_ASSERT(stack && topo && visiting && visited);

#define ENSURE_FLAGS(_id)                                                      \
  do                                                                           \
  {                                                                            \
    /* Grow only. oak_resize would truncate, losing colours, if a lower       \
     * module id turned up later; new slots are zero-filled. */               \
    if (oak_size(visiting) <= (usize)(_id))                                   \
      OAK_ASSERT(oak_resize(visiting, (usize)(_id) + 1u));                    \
    if (oak_size(visited) <= (usize)(_id))                                    \
      OAK_ASSERT(oak_resize(visited, (usize)(_id) + 1u));                     \
  } while (0)

  ENSURE_FLAGS(entry->module_id);
  OAK_DATA(char, visiting)[entry->module_id] = 1;
  {
    loader_frame_t entry_frame;
    entry_frame.mod = entry;
    entry_frame.imports = oak_vector_new(a, sizeof(loader_import_t));
    OAK_ASSERT(entry_frame.imports);
    collect_imports(entry, entry_frame.imports);
    entry_frame.next_import_idx = 0;
    OAK_ASSERT(oak_push_back(stack, &entry_frame));
  }

#define RECORD_ALIAS(_parent_mod, _imp, _dep_id)                               \
  do                                                                           \
  {                                                                            \
    const oak_token_t* _atk = loader_import_alias_token(_imp);          \
    if (_atk)                                                                  \
    {                                                                          \
      const char* _a = oak_token_text(_atk);                                   \
      const int _al = oak_token_size(_atk);                                \
      const usize _mid = (usize)(_dep_id);                                    \
      if (!oak_contains((_parent_mod)->imports, _a, (usize)_al))               \
        oak_put((_parent_mod)->imports, _a, (usize)_al, &_mid);                \
      else                                                                     \
      {                                                                        \
        loader_error(out,                                                      \
                     "%s: duplicate import alias '%.*s'",                      \
                     (_parent_mod)->dotted_name,                               \
                     (int)_al,                                                 \
                     _a);                                                      \
        rc = -1;                                                               \
      }                                                                        \
    }                                                                          \
  } while (0)

  int rc = 0;
  while (oak_size(stack) > 0 && rc == 0)
  {
    loader_frame_t* top = oak_get(stack, oak_size(stack) - 1);
    if (top->next_import_idx >= oak_size(top->imports))
    {
      OAK_DATA(char, visiting)[top->mod->module_id] = 0;
      OAK_DATA(char, visited)[top->mod->module_id] = 1;
      OAK_ASSERT(oak_push_back(topo, &top->mod));
      oak_destroy(top->imports);
      OAK_ASSERT(oak_pop_back(stack, OAK_NULL));
      continue;
    }

    const loader_import_t* imp =
        oak_cget(top->imports, top->next_import_idx++);
    if (!imp->path)
    {
      loader_error(out, "%s: malformed import", top->mod->dotted_name);
      rc = -1;
      break;
    }

    char* dotted = dotted_name_from_path(a, imp->path);
    char* mod_dir = path_dirname_dup(a, top->mod->canonical_path);

    /* A mount claims the leading segment of the dotted name, so look it up
     * before anything else. Mounts cannot be registered over a built-in native
     * module (oak_module_mount_add rejects that), so checking them first
     * cannot let a package impersonate the stdlib. */
    const char* mount_package = OAK_NULL;
    const char* mount_root = oak_module_mount_find(
        opts->module_mounts, mod_dir, dotted, strcspn(dotted, "."),
        &mount_package);

    const int is_native = !mount_root && opts_has_native_module(opts, dotted);

    char* file_path = OAK_NULL;
    if (mount_root)
    {
      /* Authoritative, like an installed stdlib: once a namespace belongs to a
       * package, a missing module in it is that package's error. Falling
       * through to a module-relative file would let an unrelated local
       * directory of the same name answer for the package. */
      file_path = path_resolve_dotted(a, mount_root, dotted);
      if (!path_exists(file_path))
      {
        if (mount_package)
          loader_error(out, "cannot find module '%s' in package '%s': %s",
                       dotted, mount_package, file_path);
        else
          loader_error(out, "cannot find mounted module '%s': %s", dotted,
                       file_path);
        oak_free(a, file_path, OAK_HERE);
        oak_free(a, mod_dir, OAK_HERE);
        oak_free(a, dotted, OAK_HERE);
        rc = -1;
        break;
      }
    }
    else if (is_native)
    {
      /* Native stdlib modules (e.g. io) resolve against the stdlib BEFORE any
       * module-relative file, so a project-local <dir>/io.oak cannot shadow the
       * trusted stdlib and an authoritative $OAK_STDLIB_DIR / installed stdlib
       * really is authoritative. A miss from an authoritative source is a hard
       * error: report the path rather than degrading to a synthetic native
       * module (which would silently drop stub-only declarations) or to a
       * different stdlib. */
      stdlib_origin_t origin = STDLIB_ORIGIN_FALLBACK;
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
        oak_free(a, file_path, OAK_HERE);
        oak_free(a, mod_dir, OAK_HERE);
        oak_free(a, dotted, OAK_HERE);
        rc = -1;
        break;
      }
    }
    else
    {
      file_path = path_resolve_dotted(a, mod_dir, dotted);
    }
    oak_free(a, mod_dir, OAK_HERE);

    if (!path_exists(file_path) && is_native)
    {
      /* Synthesizing the module from the native bindings alone drops
       * everything only the Oak stub declares (parameter mutability,
       * stub-only signatures), so it is opt-in: hosts without a
       * filesystem to load stubs from enable it explicitly. Otherwise a
       * missing stub is reported as the configuration error it is. */
      if (!opts->allow_synthetic_native_modules)
      {
        loader_error(out,
                     "cannot find stdlib module '%s': %s "
                     "(pass --allow-synthetic-modules to build it from the "
                     "native bindings instead)",
                     dotted,
                     file_path);
        oak_free(a, file_path, OAK_HERE);
        oak_free(a, dotted, OAK_HERE);
        rc = -1;
        break;
      }
      oak_module_t* dep =
          create_native_module(out_reg, opts, dotted, out);
      if (!dep || out->error_count > 0)
      {
        oak_free(a, dotted, OAK_HERE);
        rc = -1;
        break;
      }
      const oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const int alen = oak_token_size(atk);
        if (oak_contains(top->mod->imports, alias, (usize)alen))
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                       (int)alen,
                       alias);
          oak_free(a, dotted, OAK_HERE);
          rc = -1;
          break;
        }
        const usize dep_module_id = dep->module_id;
        oak_put(top->mod->imports, alias, (usize)alen, &dep_module_id);
      }
      OAK_ASSERT(oak_push_back(top->mod->import_modules, &dep->module_id));
      ENSURE_FLAGS(dep->module_id);
      OAK_DATA(char, visited)[dep->module_id] = 1;
      oak_free(a, dotted, OAK_HERE);
      oak_free(a, file_path, OAK_HERE);
      continue;
    }

    char* canonical = path_canonicalize(a, file_path);

    oak_module_t* found =
        oak_module_registry_find_by_path(out_reg, canonical);
    if (found && (usize)found->module_id < oak_size(visiting) &&
        OAK_CDATA(char, visiting)[found->module_id])
    {
      char buf[256];
      usize w = 0;
      const loader_frame_t* const frames = OAK_CDATA(loader_frame_t, stack);
      for (usize i = 0; i < oak_size(stack) && w < sizeof(buf) - 8; ++i)
      {
        const char* nm = frames[i].mod->dotted_name;
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
      oak_free(a, dotted, OAK_HERE);
      oak_free(a, file_path, OAK_HERE);
      oak_free(a, canonical, OAK_HERE);
      rc = -1;
      break;
    }

    if (found && (usize)found->module_id < oak_size(visited) &&
        OAK_CDATA(char, visited)[found->module_id])
    {
      RECORD_ALIAS(top->mod, imp, found->module_id);
      if (rc != 0)
      {
        oak_free(a, dotted, OAK_HERE);
        oak_free(a, file_path, OAK_HERE);
        oak_free(a, canonical, OAK_HERE);
        break;
      }
      OAK_ASSERT(oak_push_back(top->mod->import_modules, &found->module_id));
      oak_free(a, dotted, OAK_HERE);
      oak_free(a, file_path, OAK_HERE);
      oak_free(a, canonical, OAK_HERE);
      continue;
    }

    int dep_created = 0;
    oak_module_t* dep =
        parse_or_get_module(out_reg, canonical, dotted, 0, out, &dep_created);
    if (!dep || out->error_count > 0)
    {
      oak_free(a, dotted, OAK_HERE);
      oak_free(a, file_path, OAK_HERE);
      oak_free(a, canonical, OAK_HERE);
      rc = -1;
      break;
    }

    {
      const oak_token_t* atk = loader_import_alias_token(imp);
      if (atk)
      {
        const char* alias = oak_token_text(atk);
        const int alen = oak_token_size(atk);
        if (oak_contains(top->mod->imports, alias, (usize)alen))
        {
          loader_error(out,
                       "%s: duplicate import alias '%.*s'",
                       top->mod->dotted_name,
                       (int)alen,
                       alias);
          oak_free(a, dotted, OAK_HERE);
          oak_free(a, file_path, OAK_HERE);
          oak_free(a, canonical, OAK_HERE);
          rc = -1;
          break;
        }
        const usize dep_module_id = dep->module_id;
        oak_put(top->mod->imports, alias, (usize)alen, &dep_module_id);
      }
    }
    OAK_ASSERT(oak_push_back(top->mod->import_modules, &dep->module_id));

    oak_free(a, dotted, OAK_HERE);
    oak_free(a, file_path, OAK_HERE);
    oak_free(a, canonical, OAK_HERE);

    ENSURE_FLAGS(dep->module_id);
    if (OAK_CDATA(char, visiting)[dep->module_id] ||
        OAK_CDATA(char, visited)[dep->module_id])
      continue;
    OAK_DATA(char, visiting)[dep->module_id] = 1;
    {
      loader_frame_t f;
      f.mod = dep;
      f.imports = oak_vector_new(a, sizeof(loader_import_t));
      OAK_ASSERT(f.imports);
      collect_imports(dep, f.imports);
      f.next_import_idx = 0;
      OAK_ASSERT(oak_push_back(stack, &f));
    }
  }

  {
    const loader_frame_t* const frames = OAK_CDATA(loader_frame_t, stack);
    for (usize i = 0; i < oak_size(stack); ++i)
      oak_destroy(frames[i].imports);
  }

  oak_destroy(stack);
  oak_destroy(visiting);
  oak_destroy(visited);

#undef RECORD_ALIAS

  if (rc != 0)
  {
    oak_destroy(topo);
    return rc;
  }

  {
    /* OAK_DATA, not OAK_CDATA: with a pointer element type the const in
     * OAK_CDATA binds to the pointee rather than the pointer. */
    oak_module_t** const mods = OAK_DATA(oak_module_t*, topo);
    for (usize i = 0; i < oak_size(topo) && rc == 0; ++i)
    {
      if (compile_module(mods[i], opts, out_reg, out) != 0)
        rc = -1;
    }
  }
  oak_destroy(topo);

  if (rc == 0)
    out->entry = entry;
  return rc;

#undef ENSURE_FLAGS
}
