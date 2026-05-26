#include "internal/oak_module_loader.h"

/* ---------- DFS work structures ---------- */

struct loader_frame_t
{
  struct oak_module_t* mod;
  struct loader_import_vec_t imports;
  int next_import_idx;
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

  struct loader_frame_vec_t stack;
  oak_dynarr_init(&stack.items, &stack.count, &stack.capacity);
  struct oak_module_ptr_vec_t topo;
  oak_dynarr_init(&topo.items, &topo.count, &topo.capacity);
  struct char_vec_t visiting;
  oak_dynarr_init(&visiting.items, &visiting.count, &visiting.capacity);
  struct char_vec_t visited;
  oak_dynarr_init(&visited.items, &visited.count, &visited.capacity);

#define ENSURE_FLAGS(_id)                                                      \
  do                                                                           \
  {                                                                            \
    while (visiting.count <= (int)(_id))                                       \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_dynarr_push(a, &visiting.items,                                         \
                      &visiting.count,                                         \
                      &visiting.capacity,                                      \
                      &_zz,                                                    \
                      sizeof(char));                                           \
    }                                                                          \
    while (visited.count <= (int)(_id))                                        \
    {                                                                          \
      char _zz = 0;                                                            \
      oak_dynarr_push(a, &visited.items,                                          \
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
    oak_dynarr_push(a, &stack.items,
                    &stack.count,
                    &stack.capacity,
                    &entry_frame,
                    sizeof(entry_frame));
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
  while (stack.count > 0 && rc == 0)
  {
    struct loader_frame_t* top = &stack.items[stack.count - 1];
    if (top->next_import_idx >= top->imports.count)
    {
      visiting.items[top->mod->module_id] = 0;
      visited.items[top->mod->module_id] = 1;
      oak_dynarr_push(a, &topo.items,
                      &topo.count,
                      &topo.capacity,
                      &top->mod,
                      sizeof(top->mod));
      oak_dynarr_free(a,
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

    char* dotted = dotted_name_from_path(a, imp->path);

    char* mod_dir = path_dirname_dup(a, top->mod->canonical_path);
    char* file_path = path_resolve_dotted(a, mod_dir, dotted);
    OAK_FREE(a, mod_dir);

    if (!path_exists(file_path) && opts_has_native_module(opts, dotted))
    {
      OAK_FREE(a, file_path);
      file_path = path_resolve_dotted(a, "stdlib", dotted);
#if defined(OAK_STDLIB_DIR)
      if (!path_exists(file_path))
      {
        OAK_FREE(a, file_path);
        file_path = path_resolve_dotted(a, OAK_STDLIB_DIR, dotted);
      }
#endif
    }

    if (!path_exists(file_path) && opts_has_native_module(opts, dotted))
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
      oak_dynarr_push(a, &top->mod->import_modules.items,
                      &top->mod->import_modules.count,
                      &top->mod->import_modules.capacity,
                      &dep->module_id,
                      sizeof(dep->module_id));
      ENSURE_FLAGS(dep->module_id);
      visited.items[dep->module_id] = 1;
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      continue;
    }

    char* canonical = path_canonicalize(a, file_path);

    struct oak_module_t* found =
        oak_module_registry_find_by_path(out_reg, canonical);
    if (found && (int)found->module_id < visiting.count &&
        visiting.items[found->module_id])
    {
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
      OAK_FREE(a, dotted);
      OAK_FREE(a, file_path);
      OAK_FREE(a, canonical);
      rc = -1;
      break;
    }

    if (found && (int)found->module_id < visited.count &&
        visited.items[found->module_id])
    {
      RECORD_ALIAS(top->mod, imp, found->module_id);
      oak_dynarr_push(a, &top->mod->import_modules.items,
                      &top->mod->import_modules.count,
                      &top->mod->import_modules.capacity,
                      &found->module_id,
                      sizeof(found->module_id));
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
    oak_dynarr_push(a, &top->mod->import_modules.items,
                    &top->mod->import_modules.count,
                    &top->mod->import_modules.capacity,
                    &dep->module_id,
                    sizeof(dep->module_id));

    OAK_FREE(a, dotted);
    OAK_FREE(a, file_path);
    OAK_FREE(a, canonical);

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
      oak_dynarr_push(a,
          &stack.items, &stack.count, &stack.capacity, &f, sizeof(f));
    }
  }

  for (int i = 0; i < stack.count; ++i)
    oak_dynarr_free(a, &stack.items[i].imports.items,
                    &stack.items[i].imports.count,
                    &stack.items[i].imports.capacity);

  oak_dynarr_free(a, &stack.items, &stack.count, &stack.capacity);
  oak_dynarr_free(a, &visiting.items, &visiting.count, &visiting.capacity);
  oak_dynarr_free(a, &visited.items, &visited.count, &visited.capacity);

#undef RECORD_ALIAS

  if (rc != 0)
  {
    oak_dynarr_free(a, &topo.items, &topo.count, &topo.capacity);
    return rc;
  }

  for (int i = 0; i < topo.count && rc == 0; ++i)
  {
    if (compile_module(topo.items[i], opts, out_reg, out) != 0)
      rc = -1;
  }
  oak_dynarr_free(a, &topo.items, &topo.count, &topo.capacity);

  if (rc == 0)
    out->entry = entry;
  return rc;

#undef ENSURE_FLAGS
}
