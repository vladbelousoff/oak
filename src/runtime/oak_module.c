#include "oak_module.h"

#include "oak_allocator.h"
#include "oak_lexer.h"
#include "oak_str.h"

#include <string.h>

static char* oak_strdup_alloc(struct oak_allocator_t* a, const char* s)
{
  if (!s)
    return null;
  const usize n = strlen(s);
  char* copy = OAK_ALLOC(a, n + 1u);
  if (!copy)
    return null;
  memcpy(copy, s, n);
  copy[n] = 0;
  return copy;
}

void oak_module_registry_init(struct oak_module_registry_t* reg,
                              struct oak_allocator_t* allocator)
{
  reg->allocator = allocator;
  oak_assert(oak_dynarr_init(allocator, &reg->modules, sizeof *reg->modules));
  oak_htable_init(&reg->by_canonical_path, allocator);
}

static void oak_module_free(struct oak_module_t* mod)
{
  if (!mod)
    return;
  struct oak_allocator_t* a = mod->allocator;
  if (mod->chunk)
  {
    oak_chunk_free(mod->chunk);
    mod->chunk = null;
  }
  oak_parser_free(&mod->parser);
  if (mod->lexer)
  {
    oak_lexer_free(mod->lexer);
    mod->lexer = null;
  }
  oak_file_unmap(&mod->source);
  oak_htable_free(&mod->imports);
  oak_dynarr_free(&mod->import_modules);
  oak_type_registry_free(&mod->types);
  oak_symbol_registry_free(&mod->exports);
  if (mod->canonical_path)
    OAK_FREE(a, mod->canonical_path);
  if (mod->dotted_name)
    OAK_FREE(a, mod->dotted_name);
  OAK_FREE(a, mod);
}

void oak_module_registry_free(struct oak_module_registry_t* reg)
{
  for (int i = 0; i < oak_dynarr_count(reg->modules); ++i)
    oak_module_free(reg->modules[i]);
  oak_dynarr_free(&reg->modules);
  oak_htable_free(&reg->by_canonical_path);
}

struct oak_module_t*
oak_module_registry_get(const struct oak_module_registry_t* reg, u16 module_id)
{
  if ((int)module_id >= oak_dynarr_count(reg->modules))
    return null;
  return reg->modules[module_id];
}

struct oak_module_t*
oak_module_registry_find_by_path(const struct oak_module_registry_t* reg,
                                 const char* canonical_path)
{
  const int idx = oak_htable_get(
      &reg->by_canonical_path, canonical_path, strlen(canonical_path));
  if (idx < 0)
    return null;
  return reg->modules[idx];
}

struct oak_module_t*
oak_module_registry_create(struct oak_module_registry_t* reg,
                           const char* canonical_path,
                           const char* dotted_name)
{
  struct oak_allocator_t* a = reg->allocator;
  struct oak_module_t* mod =
      OAK_ALLOC(a, sizeof(struct oak_module_t));
  if (!mod)
    return null;
  memset(mod, 0, sizeof(*mod));
  mod->allocator = a;
  mod->canonical_path = oak_strdup_alloc(a, canonical_path);
  mod->dotted_name = oak_strdup_alloc(a, dotted_name);
  mod->module_id = (u16)oak_dynarr_count(reg->modules);
  mod->state = OAK_MOD_PARSED;
  oak_htable_init(&mod->imports, a);
  oak_symbol_registry_init(&mod->exports, a);
  oak_assert(oak_dynarr_init(a, &mod->import_modules, sizeof *mod->import_modules));

  oak_assert(oak_dynarr_push(&reg->modules, &mod));
  oak_htable_insert(&reg->by_canonical_path,
                    mod->canonical_path,
                    strlen(mod->canonical_path),
                    (int)mod->module_id);
  return mod;
}

const struct oak_symbol_t* oak_module_find_export_symbol(
    const struct oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find(&mod->exports, name);
}

const struct oak_module_export_fn_t* oak_module_find_export_fn(
    const struct oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_fn(&mod->exports, name);
}

const struct oak_module_export_record_t* oak_module_find_export_record(
    const struct oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_record(&mod->exports, name);
}

const struct oak_module_export_enum_t* oak_module_find_export_enum(
    const struct oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_enum(&mod->exports, name);
}

const struct oak_module_export_trait_t* oak_module_find_export_trait(
    const struct oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_trait(&mod->exports, name);
}
