#include "oak_symbol.h"

#include "oak_allocator.h"
#include "oak_log.h"
#include "oak_module_impl.h"

#include <string.h>

void oak_symbol_registry_init(oak_symbol_registry_t* registry,
                              oak_allocator_t* allocator)
{
  registry->allocator = allocator;
  registry->by_name = oak_hash_map_new(allocator, sizeof(usize));
  registry->symbols =
      oak_vector_new(allocator, sizeof(oak_symbol_t));
  registry->fns =
      oak_vector_new(allocator, sizeof(oak_module_export_fn_t));
  registry->records =
      oak_vector_new(allocator, sizeof(oak_module_export_record_t));
  registry->enums =
      oak_vector_new(allocator, sizeof(oak_module_export_enum_t));
  registry->interfaces = oak_vector_new(
      allocator, sizeof(oak_module_export_interface_t));
  oak_assert(registry->by_name && registry->symbols && registry->fns &&
             registry->records && registry->enums && registry->interfaces);
}

static void free_fn_entry(oak_allocator_t* a,
                          oak_module_export_fn_t* fn)
{
  if (fn->stub_attrs)
    oak_free(a, fn->stub_attrs, OAK_HERE);
  if (fn->param_types)
    oak_free(a, fn->param_types, OAK_HERE);
  if (fn->param_mut_flags)
    oak_free(a, fn->param_mut_flags, OAK_HERE);
}

static void free_record_entry(oak_allocator_t* a,
                              oak_module_export_record_t* rec)
{
  oak_destroy(rec->fields);
  oak_module_export_record_method_t* methods =
      OAK_DATA(oak_module_export_record_method_t, rec->methods);
  for (usize mi = 0; mi < oak_size(rec->methods); ++mi)
  {
    if (methods[mi].stub_attrs)
      oak_free(a, methods[mi].stub_attrs, OAK_HERE);
    if (methods[mi].param_types)
      oak_free(a, methods[mi].param_types, OAK_HERE);
    if (methods[mi].param_mut_flags)
      oak_free(a, methods[mi].param_mut_flags, OAK_HERE);
  }
  oak_destroy(rec->methods);
}

static void free_enum_entry(oak_module_export_enum_t* en)
{
  oak_destroy(en->variants);
}

static void free_interface_entry(oak_allocator_t* a,
                             oak_module_export_interface_t* tr)
{
  oak_module_export_interface_method_t* methods =
      OAK_DATA(oak_module_export_interface_method_t, tr->methods);
  for (usize mi = 0; mi < oak_size(tr->methods); ++mi)
  {
    if (methods[mi].param_types)
      oak_free(a, methods[mi].param_types, OAK_HERE);
  }
  oak_destroy(tr->methods);
}

void oak_symbol_registry_free(oak_symbol_registry_t* registry)
{
  oak_allocator_t* a = registry->allocator;

  oak_module_export_fn_t* fns =
      OAK_DATA(oak_module_export_fn_t, registry->fns);
  for (usize i = 0; i < oak_size(registry->fns); ++i)
    free_fn_entry(a, &fns[i]);
  oak_destroy(registry->fns);

  oak_module_export_record_t* records =
      OAK_DATA(oak_module_export_record_t, registry->records);
  for (usize i = 0; i < oak_size(registry->records); ++i)
    free_record_entry(a, &records[i]);
  oak_destroy(registry->records);

  oak_module_export_enum_t* enums =
      OAK_DATA(oak_module_export_enum_t, registry->enums);
  for (usize i = 0; i < oak_size(registry->enums); ++i)
    free_enum_entry(&enums[i]);
  oak_destroy(registry->enums);

  oak_module_export_interface_t* interfaces =
      OAK_DATA(oak_module_export_interface_t, registry->interfaces);
  for (usize i = 0; i < oak_size(registry->interfaces); ++i)
    free_interface_entry(a, &interfaces[i]);
  oak_destroy(registry->interfaces);

  oak_destroy(registry->by_name);
  oak_destroy(registry->symbols);
}

oak_symbol_t* oak_symbol_registry_insert(
    oak_symbol_registry_t* registry, const oak_symbol_t* symbol)
{
  if (!symbol || !symbol->name || !symbol->name[0])
    return null;
  if (oak_symbol_registry_find(registry, symbol->name))
    return null;
  oak_assert(oak_push_back(registry->symbols, symbol));
  const usize index = oak_size(registry->symbols) - 1;
  oak_symbol_t* stored = oak_get(registry->symbols, index);
  oak_assert(oak_put_str(registry->by_name, stored->name, &index));
  return stored;
}

const oak_symbol_t* oak_symbol_registry_find(
    const oak_symbol_registry_t* registry, const char* name)
{
  const usize* index = oak_cfind_str(registry->by_name, name);
  return index ? oak_cget(registry->symbols, *index) : null;
}

oak_module_export_fn_t*
oak_symbol_registry_insert_fn(oak_symbol_registry_t* registry,
                              const char* name,
                              u16 owner_module_id,
                              const oak_module_export_fn_t* fn)
{
  const usize idx = oak_size(registry->fns);
  oak_assert(oak_push_back(registry->fns, fn));
  oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_FUNCTION,
    .owner_module_id = owner_module_id,
    .payload_index = (int)idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return oak_get(registry->fns, idx);
}

oak_module_export_record_t*
oak_symbol_registry_insert_record(oak_symbol_registry_t* registry,
                                  const char* name,
                                  u16 owner_module_id,
                                  const oak_module_export_record_t* rec)
{
  const usize idx = oak_size(registry->records);
  oak_assert(oak_push_back(registry->records, rec));
  oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_RECORD,
    .owner_module_id = owner_module_id,
    .payload_index = (int)idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return oak_get(registry->records, idx);
}

oak_module_export_enum_t*
oak_symbol_registry_insert_enum(oak_symbol_registry_t* registry,
                                const char* name,
                                u16 owner_module_id,
                                const oak_module_export_enum_t* en)
{
  const usize idx = oak_size(registry->enums);
  oak_assert(oak_push_back(registry->enums, en));
  oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_ENUM,
    .owner_module_id = owner_module_id,
    .payload_index = (int)idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return oak_get(registry->enums, idx);
}

oak_module_export_interface_t*
oak_symbol_registry_insert_interface(oak_symbol_registry_t* registry,
                                 const char* name,
                                 u16 owner_module_id,
                                 const oak_module_export_interface_t* tr)
{
  const usize idx = oak_size(registry->interfaces);
  oak_assert(oak_push_back(registry->interfaces, tr));
  oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_INTERFACE,
    .owner_module_id = owner_module_id,
    .payload_index = (int)idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return oak_get(registry->interfaces, idx);
}


const oak_module_export_fn_t*
oak_symbol_registry_find_fn(const oak_symbol_registry_t* registry,
                            const char* name)
{
  const oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_FUNCTION)
    return null;
  return oak_cget(registry->fns, (usize)s->payload_index);
}

const oak_module_export_record_t*
oak_symbol_registry_find_record(const oak_symbol_registry_t* registry,
                                const char* name)
{
  const oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_RECORD)
    return null;
  return oak_cget(registry->records, (usize)s->payload_index);
}

const oak_module_export_enum_t*
oak_symbol_registry_find_enum(const oak_symbol_registry_t* registry,
                              const char* name)
{
  const oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_ENUM)
    return null;
  return oak_cget(registry->enums, (usize)s->payload_index);
}

const oak_module_export_interface_t*
oak_symbol_registry_find_interface(const oak_symbol_registry_t* registry,
                               const char* name)
{
  const oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_INTERFACE)
    return null;
  return oak_cget(registry->interfaces, (usize)s->payload_index);
}
