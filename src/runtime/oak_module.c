#include "oak_module.h"

#include "oak_allocator.h"
#include "oak_lexer.h"

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
  oak_dynarr_init(
      &reg->modules.items, &reg->modules.count, &reg->modules.capacity);
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
  oak_dynarr_free(a, &mod->import_modules.items,
                  &mod->import_modules.count,
                  &mod->import_modules.capacity);
  oak_type_registry_free(&mod->types);
  oak_htable_free(&mod->exports_fn.by_name);
  for (int i = 0; i < mod->exports_fn.count; ++i)
  {
    if (mod->exports_fn.items[i].stub_attrs)
      OAK_FREE(a, mod->exports_fn.items[i].stub_attrs);
    if (mod->exports_fn.items[i].param_types)
      OAK_FREE(a, mod->exports_fn.items[i].param_types);
    if (mod->exports_fn.items[i].param_mut_flags)
      OAK_FREE(a, mod->exports_fn.items[i].param_mut_flags);
  }
  oak_dynarr_free(a, &mod->exports_fn.items,
                  &mod->exports_fn.count,
                  &mod->exports_fn.capacity);
  oak_htable_free(&mod->exports_record.by_name);
  for (int i = 0; i < mod->exports_record.count; ++i)
  {
    oak_dynarr_free(a, &mod->exports_record.items[i].fields,
                    &mod->exports_record.items[i].field_count,
                    &mod->exports_record.items[i].field_capacity);
    for (int mi = 0; mi < mod->exports_record.items[i].method_count; ++mi)
    {
      if (mod->exports_record.items[i].methods[mi].stub_attrs)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].stub_attrs);
      if (mod->exports_record.items[i].methods[mi].param_types)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].param_types);
      if (mod->exports_record.items[i].methods[mi].param_mut_flags)
        OAK_FREE(a, mod->exports_record.items[i].methods[mi].param_mut_flags);
    }
    oak_dynarr_free(a, &mod->exports_record.items[i].methods,
                    &mod->exports_record.items[i].method_count,
                    &mod->exports_record.items[i].method_capacity);
  }
  oak_dynarr_free(a, &mod->exports_record.items,
                  &mod->exports_record.count,
                  &mod->exports_record.capacity);
  oak_htable_free(&mod->exports_enum.by_name);
  for (int i = 0; i < mod->exports_enum.count; ++i)
    oak_dynarr_free(a, &mod->exports_enum.items[i].variants,
                    &mod->exports_enum.items[i].variant_count,
                    &mod->exports_enum.items[i].variant_capacity);
  oak_dynarr_free(a, &mod->exports_enum.items,
                  &mod->exports_enum.count,
                  &mod->exports_enum.capacity);
  oak_htable_free(&mod->exports_trait.by_name);
  for (int i = 0; i < mod->exports_trait.count; ++i)
  {
    for (int mi = 0; mi < mod->exports_trait.items[i].method_count; ++mi)
    {
      if (mod->exports_trait.items[i].methods[mi].param_types)
        OAK_FREE(a, mod->exports_trait.items[i].methods[mi].param_types);
    }
    oak_dynarr_free(a, &mod->exports_trait.items[i].methods,
                    &mod->exports_trait.items[i].method_count,
                    &mod->exports_trait.items[i].method_capacity);
  }
  oak_dynarr_free(a, &mod->exports_trait.items,
                  &mod->exports_trait.count,
                  &mod->exports_trait.capacity);
  if (mod->canonical_path)
    OAK_FREE(a, mod->canonical_path);
  if (mod->dotted_name)
    OAK_FREE(a, mod->dotted_name);
  OAK_FREE(a, mod);
}

void oak_module_registry_free(struct oak_module_registry_t* reg)
{
  for (int i = 0; i < reg->modules.count; ++i)
    oak_module_free(reg->modules.items[i]);
  oak_dynarr_free(reg->allocator,
      &reg->modules.items, &reg->modules.count, &reg->modules.capacity);
  oak_htable_free(&reg->by_canonical_path);
}

struct oak_module_t*
oak_module_registry_get(const struct oak_module_registry_t* reg, u16 module_id)
{
  if ((int)module_id >= reg->modules.count)
    return null;
  return reg->modules.items[module_id];
}

struct oak_module_t*
oak_module_registry_find_by_path(const struct oak_module_registry_t* reg,
                                 const char* canonical_path)
{
  const int idx = oak_htable_get(
      &reg->by_canonical_path, canonical_path, strlen(canonical_path));
  if (idx < 0)
    return null;
  return reg->modules.items[idx];
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
  mod->module_id = (u16)reg->modules.count;
  mod->state = OAK_MOD_PARSED;
  oak_htable_init(&mod->imports, a);
  oak_dynarr_init(&mod->import_modules.items,
                  &mod->import_modules.count,
                  &mod->import_modules.capacity);
  oak_htable_init(&mod->exports_fn.by_name, a);
  oak_dynarr_init(&mod->exports_fn.items,
                  &mod->exports_fn.count,
                  &mod->exports_fn.capacity);
  oak_htable_init(&mod->exports_record.by_name, a);
  oak_dynarr_init(&mod->exports_record.items,
                  &mod->exports_record.count,
                  &mod->exports_record.capacity);
  oak_htable_init(&mod->exports_enum.by_name, a);
  oak_dynarr_init(&mod->exports_enum.items,
                  &mod->exports_enum.count,
                  &mod->exports_enum.capacity);
  oak_htable_init(&mod->exports_trait.by_name, a);
  oak_dynarr_init(&mod->exports_trait.items,
                  &mod->exports_trait.count,
                  &mod->exports_trait.capacity);

  oak_dynarr_push(a, &reg->modules.items,
                  &reg->modules.count,
                  &reg->modules.capacity,
                  &mod,
                  sizeof(mod));
  oak_htable_insert(&reg->by_canonical_path,
                    mod->canonical_path,
                    strlen(mod->canonical_path),
                    (int)mod->module_id);
  return mod;
}

const struct oak_module_export_fn_t* oak_module_find_export_fn(
    const struct oak_module_t* mod, const char* name, usize name_len)
{
  const int idx = oak_htable_get(&mod->exports_fn.by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_fn.items[idx];
}

const struct oak_module_export_record_t* oak_module_find_export_record(
    const struct oak_module_t* mod, const char* name, usize name_len)
{
  const int idx = oak_htable_get(&mod->exports_record.by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_record.items[idx];
}

const struct oak_module_export_enum_t* oak_module_find_export_enum(
    const struct oak_module_t* mod, const char* name, usize name_len)
{
  const int idx = oak_htable_get(&mod->exports_enum.by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_enum.items[idx];
}

const struct oak_module_export_trait_t* oak_module_find_export_trait(
    const struct oak_module_t* mod, const char* name, usize name_len)
{
  const int idx = oak_htable_get(&mod->exports_trait.by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_trait.items[idx];
}
