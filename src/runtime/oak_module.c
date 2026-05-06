#include "oak_module.h"

#include "oak_lexer.h"
#include "oak_mem.h"

#include <string.h>

static char* oak_strdup_loc(const char* s)
{
  if (!s)
    return null;
  const usize n = strlen(s);
  char* copy = oak_alloc(n + 1u, OAK_SRC_LOC);
  if (!copy)
    return null;
  memcpy(copy, s, n);
  copy[n] = 0;
  return copy;
}

void oak_module_registry_init(struct oak_module_registry_t* reg)
{
  oak_dynarr_init(&reg->modules.items, &reg->modules.count, &reg->modules.capacity);
  oak_hash_table_init(&reg->by_canonical_path);
}

static void oak_module_free(struct oak_module_t* mod)
{
  if (!mod)
    return;
  if (mod->chunk)
  {
    oak_chunk_free(mod->chunk); /* frees both internals and the chunk struct */
    mod->chunk = null;
  }
  oak_parser_free(&mod->parser);
  if (mod->lexer)
  {
    oak_lexer_free(mod->lexer);
    mod->lexer = null;
  }
  oak_file_unmap(&mod->source);
  oak_hash_table_free(&mod->imports);
  oak_dynarr_free(&mod->import_modules.items, &mod->import_modules.count, &mod->import_modules.capacity);
  oak_hash_table_free(&mod->exports_fn_by_name);
  oak_dynarr_free(&mod->exports_fn.items, &mod->exports_fn.count, &mod->exports_fn.capacity);
  oak_hash_table_free(&mod->exports_record_by_name);
  oak_dynarr_free(&mod->exports_record.items, &mod->exports_record.count, &mod->exports_record.capacity);
  oak_hash_table_free(&mod->exports_enum_by_name);
  oak_dynarr_free(&mod->exports_enum.items, &mod->exports_enum.count, &mod->exports_enum.capacity);
  if (mod->canonical_path)
    oak_free(mod->canonical_path, OAK_SRC_LOC);
  if (mod->dotted_name)
    oak_free(mod->dotted_name, OAK_SRC_LOC);
  oak_free(mod, OAK_SRC_LOC);
}

void oak_module_registry_free(struct oak_module_registry_t* reg)
{
  for (int i = 0; i < reg->modules.count; ++i)
    oak_module_free(reg->modules.items[i]);
  oak_dynarr_free(&reg->modules.items, &reg->modules.count, &reg->modules.capacity);
  oak_hash_table_free(&reg->by_canonical_path);
}

struct oak_module_t*
oak_module_registry_get(const struct oak_module_registry_t* reg, u16 module_id)
{
  if ((int)module_id >= reg->modules.count)
    return null;
  return reg->modules.items[module_id];
}

struct oak_module_t* oak_module_registry_find_by_path(
    const struct oak_module_registry_t* reg, const char* canonical_path)
{
  const int idx = oak_hash_table_get(
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
  struct oak_module_t* mod =
      oak_alloc(sizeof(struct oak_module_t), OAK_SRC_LOC);
  if (!mod)
    return null;
  memset(mod, 0, sizeof(*mod));
  mod->canonical_path = oak_strdup_loc(canonical_path);
  mod->dotted_name = oak_strdup_loc(dotted_name);
  mod->module_id = (u16)reg->modules.count;
  mod->state = OAK_MOD_PARSED;
  oak_hash_table_init(&mod->imports);
  oak_dynarr_init(&mod->import_modules.items, &mod->import_modules.count, &mod->import_modules.capacity);
  oak_hash_table_init(&mod->exports_fn_by_name);
  oak_dynarr_init(&mod->exports_fn.items, &mod->exports_fn.count, &mod->exports_fn.capacity);
  oak_hash_table_init(&mod->exports_record_by_name);
  oak_dynarr_init(&mod->exports_record.items, &mod->exports_record.count, &mod->exports_record.capacity);
  oak_hash_table_init(&mod->exports_enum_by_name);
  oak_dynarr_init(&mod->exports_enum.items, &mod->exports_enum.count, &mod->exports_enum.capacity);

  oak_dynarr_push(&reg->modules.items, &reg->modules.count, &reg->modules.capacity, &mod, sizeof(mod));
  oak_hash_table_insert(&reg->by_canonical_path,
                        mod->canonical_path,
                        strlen(mod->canonical_path),
                        (int)mod->module_id);
  return mod;
}

const struct oak_module_export_fn_t*
oak_module_find_export_fn(const struct oak_module_t* mod,
                          const char* name,
                          usize name_len)
{
  const int idx =
      oak_hash_table_get(&mod->exports_fn_by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_fn.items[idx];
}

const struct oak_module_export_record_t*
oak_module_find_export_record(const struct oak_module_t* mod,
                              const char* name,
                              usize name_len)
{
  const int idx =
      oak_hash_table_get(&mod->exports_record_by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_record.items[idx];
}

const struct oak_module_export_enum_t*
oak_module_find_export_enum(const struct oak_module_t* mod,
                            const char* name,
                            usize name_len)
{
  const int idx =
      oak_hash_table_get(&mod->exports_enum_by_name, name, name_len);
  if (idx < 0)
    return null;
  return &mod->exports_enum.items[idx];
}
