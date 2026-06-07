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
  oak_symbol_registry_free(&mod->symbols);
  for (int i = 0; i < oak_dynarr_count(mod->exports_fn.items); ++i)
  {
    if (mod->exports_fn.items[i].stub_attrs)
      OAK_FREE(a, mod->exports_fn.items[i].stub_attrs);
    if (mod->exports_fn.items[i].param_types)
      OAK_FREE(a, mod->exports_fn.items[i].param_types);
    if (mod->exports_fn.items[i].param_mut_flags)
      OAK_FREE(a, mod->exports_fn.items[i].param_mut_flags);
  }
  oak_dynarr_free(&mod->exports_fn.items);
  for (int i = 0; i < oak_dynarr_count(mod->exports_record.items); ++i)
  {
    oak_dynarr_free(&mod->exports_record.items[i].fields);
    for (int mi = 0; mi < oak_dynarr_count(mod->exports_record.items[i].methods); ++mi)
    {
      if (mod->exports_record.items[i].methods[mi].stub_attrs)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].stub_attrs);
      if (mod->exports_record.items[i].methods[mi].param_types)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].param_types);
      if (mod->exports_record.items[i].methods[mi].param_mut_flags)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].param_mut_flags);
    }
    oak_dynarr_free(&mod->exports_record.items[i].methods);
  }
  oak_dynarr_free(&mod->exports_record.items);
  for (int i = 0; i < oak_dynarr_count(mod->exports_enum.items); ++i)
    oak_dynarr_free(&mod->exports_enum.items[i].variants);
  oak_dynarr_free(&mod->exports_enum.items);
  for (int i = 0; i < oak_dynarr_count(mod->exports_trait.items); ++i)
  {
    for (int mi = 0; mi < oak_dynarr_count(mod->exports_trait.items[i].methods); ++mi)
    {
      if (mod->exports_trait.items[i].methods[mi].param_types)
        OAK_FREE(a, mod->exports_trait.items[i].methods[mi].param_types);
    }
    oak_dynarr_free(&mod->exports_trait.items[i].methods);
  }
  oak_dynarr_free(&mod->exports_trait.items);
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
  oak_symbol_registry_init(&mod->symbols, a);
  oak_assert(oak_dynarr_init(a, &mod->import_modules, sizeof *mod->import_modules));
  oak_assert(oak_dynarr_init(a, &mod->exports_fn.items, sizeof *mod->exports_fn.items));
  oak_assert(oak_dynarr_init(a, &mod->exports_record.items, sizeof *mod->exports_record.items));
  oak_assert(oak_dynarr_init(a, &mod->exports_enum.items, sizeof *mod->exports_enum.items));
  oak_assert(oak_dynarr_init(a, &mod->exports_trait.items, sizeof *mod->exports_trait.items));

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
  const struct oak_symbol_t* symbol =
      oak_symbol_registry_find(&mod->symbols, name);
  return symbol && symbol->is_exported ? symbol : null;
}

const struct oak_module_export_fn_t* oak_module_find_export_fn(
    const struct oak_module_t* mod, const char* name)
{
  const struct oak_symbol_t* symbol = oak_module_find_export_symbol(mod, name);
  if (!symbol || symbol->kind != OAK_SYMBOL_FUNCTION)
    return null;
  return &mod->exports_fn.items[symbol->payload_index];
}

const struct oak_module_export_record_t* oak_module_find_export_record(
    const struct oak_module_t* mod, const char* name)
{
  const struct oak_symbol_t* symbol = oak_module_find_export_symbol(mod, name);
  if (!symbol || symbol->kind != OAK_SYMBOL_RECORD)
    return null;
  return &mod->exports_record.items[symbol->payload_index];
}

const struct oak_module_export_enum_t* oak_module_find_export_enum(
    const struct oak_module_t* mod, const char* name)
{
  const struct oak_symbol_t* symbol = oak_module_find_export_symbol(mod, name);
  if (!symbol || symbol->kind != OAK_SYMBOL_ENUM)
    return null;
  return &mod->exports_enum.items[symbol->payload_index];
}

const struct oak_module_export_trait_t* oak_module_find_export_trait(
    const struct oak_module_t* mod, const char* name)
{
  const struct oak_symbol_t* symbol = oak_module_find_export_symbol(mod, name);
  if (!symbol || symbol->kind != OAK_SYMBOL_TRAIT)
    return null;
  return &mod->exports_trait.items[symbol->payload_index];
}
