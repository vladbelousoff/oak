#include "oak_module_impl.h"

#include "oak_allocator.h"
#include "oak_chunk_impl.h"
#include "oak_lexer.h"
#include "oak_log.h"
#include "oak_str.h"

#include <string.h>

static char* oak_strdup_alloc(oak_allocator_t* a, const char* s)
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

void oak_module_registry_init(oak_module_registry_t* reg,
                              oak_allocator_t* allocator)
{
  reg->allocator = allocator;
  reg->modules = oak_vector_new(allocator, sizeof(oak_module_t*));
  reg->by_canonical_path = oak_hash_map_new(allocator, sizeof(usize));
  oak_assert(reg->modules && reg->by_canonical_path);
}

static void oak_module_free(oak_module_t* mod)
{
  if (!mod)
    return;
  oak_allocator_t* a = mod->allocator;
  if (mod->chunk)
  {
    oak_chunk_free(mod->chunk);
    mod->chunk = null;
  }
  oak_parser_free(mod->parser);
  mod->parser = null;
  if (mod->lexer)
  {
    oak_lexer_free(mod->lexer);
    mod->lexer = null;
  }
  oak_file_unmap(&mod->source);
  oak_destroy(mod->imports);
  oak_destroy(mod->import_modules);
  oak_type_registry_free(&mod->types);
  oak_symbol_registry_free(&mod->exports);
  if (mod->canonical_path)
    OAK_FREE(a, mod->canonical_path);
  if (mod->dotted_name)
    OAK_FREE(a, mod->dotted_name);
  OAK_FREE(a, mod);
}

void oak_module_registry_free(oak_module_registry_t* reg)
{
  oak_module_t** modules = OAK_DATA(oak_module_t*, reg->modules);
  for (usize i = 0; i < oak_size(reg->modules); ++i)
    oak_module_free(modules[i]);
  oak_destroy(reg->modules);
  oak_destroy(reg->by_canonical_path);
}

oak_module_t*
oak_module_registry_get(const oak_module_registry_t* reg, u16 module_id)
{
  oak_module_t* const* slot = oak_cget(reg->modules, module_id);
  return slot ? *slot : null;
}

oak_module_t*
oak_module_registry_find_by_path(const oak_module_registry_t* reg,
                                 const char* canonical_path)
{
  const usize* idx = oak_cfind_str(reg->by_canonical_path, canonical_path);
  if (!idx)
    return null;
  oak_module_t* const* slot = oak_cget(reg->modules, *idx);
  return slot ? *slot : null;
}

oak_module_t*
oak_module_registry_new(oak_module_registry_t* reg,
                           const char* canonical_path,
                           const char* dotted_name)
{
  oak_allocator_t* a = reg->allocator;
  oak_module_t* mod =
      OAK_ALLOC(a, sizeof(oak_module_t));
  if (!mod)
    return null;
  memset(mod, 0, sizeof(*mod));
  mod->allocator = a;
  mod->canonical_path = oak_strdup_alloc(a, canonical_path);
  mod->dotted_name = oak_strdup_alloc(a, dotted_name);
  mod->module_id = (u16)oak_size(reg->modules);
  mod->state = OAK_MOD_PARSED;
  mod->imports = oak_hash_map_new(a, sizeof(usize));
  mod->import_modules = oak_vector_new(a, sizeof(u16));
  oak_assert(mod->imports && mod->import_modules);
  oak_symbol_registry_init(&mod->exports, a);

  oak_assert(oak_push_back(reg->modules, &mod));
  const usize module_index = mod->module_id;
  oak_assert(
      oak_put_str(reg->by_canonical_path, mod->canonical_path, &module_index));
  return mod;
}

const oak_symbol_t* oak_module_find_export_symbol(
    const oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find(&mod->exports, name);
}

const oak_module_export_fn_t* oak_module_find_export_fn(
    const oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_fn(&mod->exports, name);
}

const oak_module_export_record_t* oak_module_find_export_record(
    const oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_record(&mod->exports, name);
}

const oak_module_export_enum_t* oak_module_find_export_enum(
    const oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_enum(&mod->exports, name);
}

const oak_module_export_interface_t* oak_module_find_export_interface(
    const oak_module_t* mod, const char* name)
{
  return oak_symbol_registry_find_interface(&mod->exports, name);
}

/* Accessors for the opaque oak_module_t (see include/oak_module.h). */

oak_chunk_t* oak_module_chunk(const oak_module_t* mod)
{
  return mod ? mod->chunk : null;
}

const char* oak_module_dotted_name(const oak_module_t* mod)
{
  return mod ? mod->dotted_name : null;
}

const char* oak_module_path(const oak_module_t* mod)
{
  return mod ? mod->canonical_path : null;
}

u16 oak_module_id(const oak_module_t* mod)
{
  return mod ? mod->module_id : OAK_MODULE_ID_NONE;
}

int oak_module_is_entry(const oak_module_t* mod)
{
  return mod ? mod->is_entry : 0;
}
