#include "oak_symbol.h"

#include "oak_allocator.h"
#include "oak_dynarr.h"
#include "oak_log.h"
#include "oak_module.h"

#include <string.h>

void oak_symbol_registry_init(struct oak_symbol_registry_t* registry,
                              struct oak_allocator_t* allocator)
{
  registry->allocator = allocator;
  oak_htable_init(&registry->by_name, allocator);
  oak_assert(oak_dynarr_init(
      allocator, &registry->symbols, sizeof *registry->symbols));
  registry->fns = null;
  registry->records = null;
  registry->enums = null;
  registry->interfaces = null;
}

static void free_fn_entry(struct oak_allocator_t* a,
                          struct oak_module_export_fn_t* fn)
{
  if (fn->stub_attrs)
    OAK_FREE(a, fn->stub_attrs);
  if (fn->param_types)
    OAK_FREE(a, fn->param_types);
  if (fn->param_mut_flags)
    OAK_FREE(a, fn->param_mut_flags);
}

static void free_record_entry(struct oak_allocator_t* a,
                              struct oak_module_export_record_t* rec)
{
  oak_dynarr_free(&rec->fields);
  for (int mi = 0; mi < oak_dynarr_count(rec->methods); ++mi)
  {
    if (rec->methods[mi].stub_attrs)
      OAK_FREE(a, rec->methods[mi].stub_attrs);
    if (rec->methods[mi].param_types)
      OAK_FREE(a, rec->methods[mi].param_types);
    if (rec->methods[mi].param_mut_flags)
      OAK_FREE(a, rec->methods[mi].param_mut_flags);
  }
  oak_dynarr_free(&rec->methods);
}

static void free_enum_entry(struct oak_module_export_enum_t* en)
{
  oak_dynarr_free(&en->variants);
}

static void free_interface_entry(struct oak_allocator_t* a,
                             struct oak_module_export_interface_t* tr)
{
  for (int mi = 0; mi < oak_dynarr_count(tr->methods); ++mi)
  {
    if (tr->methods[mi].param_types)
      OAK_FREE(a, tr->methods[mi].param_types);
  }
  oak_dynarr_free(&tr->methods);
}

void oak_symbol_registry_free(struct oak_symbol_registry_t* registry)
{
  struct oak_allocator_t* a = registry->allocator;
  if (registry->fns)
  {
    for (int i = 0; i < oak_dynarr_count(registry->fns); ++i)
      free_fn_entry(a, &registry->fns[i]);
    oak_dynarr_free(&registry->fns);
  }
  if (registry->records)
  {
    for (int i = 0; i < oak_dynarr_count(registry->records); ++i)
      free_record_entry(a, &registry->records[i]);
    oak_dynarr_free(&registry->records);
  }
  if (registry->enums)
  {
    for (int i = 0; i < oak_dynarr_count(registry->enums); ++i)
      free_enum_entry(&registry->enums[i]);
    oak_dynarr_free(&registry->enums);
  }
  if (registry->interfaces)
  {
    for (int i = 0; i < oak_dynarr_count(registry->interfaces); ++i)
      free_interface_entry(a, &registry->interfaces[i]);
    oak_dynarr_free(&registry->interfaces);
  }
  oak_htable_free(&registry->by_name);
  oak_dynarr_free(&registry->symbols);
}

struct oak_symbol_t* oak_symbol_registry_insert(
    struct oak_symbol_registry_t* registry, const struct oak_symbol_t* symbol)
{
  if (!symbol || !symbol->name || !symbol->name[0])
    return null;
  if (oak_symbol_registry_find(registry, symbol->name))
    return null;
  oak_assert(oak_dynarr_push(&registry->symbols, symbol));
  const int index = oak_dynarr_count(registry->symbols) - 1;
  oak_htable_insert(&registry->by_name, symbol->name, strlen(symbol->name), index);
  return &registry->symbols[index];
}

const struct oak_symbol_t* oak_symbol_registry_find(
    const struct oak_symbol_registry_t* registry, const char* name)
{
  const int index = oak_htable_get(&registry->by_name, name, strlen(name));
  if (index < 0)
    return null;
  return &registry->symbols[index];
}


static void ensure_fns_init(struct oak_symbol_registry_t* r)
{
  if (!r->fns)
    oak_assert(oak_dynarr_init(r->allocator, &r->fns, sizeof *r->fns));
}

static void ensure_records_init(struct oak_symbol_registry_t* r)
{
  if (!r->records)
    oak_assert(oak_dynarr_init(r->allocator, &r->records, sizeof *r->records));
}

static void ensure_enums_init(struct oak_symbol_registry_t* r)
{
  if (!r->enums)
    oak_assert(oak_dynarr_init(r->allocator, &r->enums, sizeof *r->enums));
}

static void ensure_interfaces_init(struct oak_symbol_registry_t* r)
{
  if (!r->interfaces)
    oak_assert(oak_dynarr_init(r->allocator, &r->interfaces, sizeof *r->interfaces));
}

struct oak_module_export_fn_t*
oak_symbol_registry_insert_fn(struct oak_symbol_registry_t* registry,
                              const char* name,
                              u16 owner_module_id,
                              const struct oak_module_export_fn_t* fn)
{
  ensure_fns_init(registry);
  const int idx = oak_dynarr_count(registry->fns);
  oak_assert(oak_dynarr_push(&registry->fns, fn));
  struct oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_FUNCTION,
    .owner_module_id = owner_module_id,
    .payload_index = idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return &registry->fns[idx];
}

struct oak_module_export_record_t*
oak_symbol_registry_insert_record(struct oak_symbol_registry_t* registry,
                                  const char* name,
                                  u16 owner_module_id,
                                  const struct oak_module_export_record_t* rec)
{
  ensure_records_init(registry);
  const int idx = oak_dynarr_count(registry->records);
  oak_assert(oak_dynarr_push(&registry->records, rec));
  struct oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_RECORD,
    .owner_module_id = owner_module_id,
    .payload_index = idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return &registry->records[idx];
}

struct oak_module_export_enum_t*
oak_symbol_registry_insert_enum(struct oak_symbol_registry_t* registry,
                                const char* name,
                                u16 owner_module_id,
                                const struct oak_module_export_enum_t* en)
{
  ensure_enums_init(registry);
  const int idx = oak_dynarr_count(registry->enums);
  oak_assert(oak_dynarr_push(&registry->enums, en));
  struct oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_ENUM,
    .owner_module_id = owner_module_id,
    .payload_index = idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return &registry->enums[idx];
}

struct oak_module_export_interface_t*
oak_symbol_registry_insert_interface(struct oak_symbol_registry_t* registry,
                                 const char* name,
                                 u16 owner_module_id,
                                 const struct oak_module_export_interface_t* tr)
{
  ensure_interfaces_init(registry);
  const int idx = oak_dynarr_count(registry->interfaces);
  oak_assert(oak_dynarr_push(&registry->interfaces, tr));
  struct oak_symbol_t symbol = {
    .name = name,
    .kind = OAK_SYMBOL_INTERFACE,
    .owner_module_id = owner_module_id,
    .payload_index = idx,
    .is_exported = 1,
  };
  if (!oak_symbol_registry_insert(registry, &symbol))
    return null;
  return &registry->interfaces[idx];
}


const struct oak_module_export_fn_t*
oak_symbol_registry_find_fn(const struct oak_symbol_registry_t* registry,
                            const char* name)
{
  const struct oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_FUNCTION)
    return null;
  return &registry->fns[s->payload_index];
}

const struct oak_module_export_record_t*
oak_symbol_registry_find_record(const struct oak_symbol_registry_t* registry,
                                const char* name)
{
  const struct oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_RECORD)
    return null;
  return &registry->records[s->payload_index];
}

const struct oak_module_export_enum_t*
oak_symbol_registry_find_enum(const struct oak_symbol_registry_t* registry,
                              const char* name)
{
  const struct oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_ENUM)
    return null;
  return &registry->enums[s->payload_index];
}

const struct oak_module_export_interface_t*
oak_symbol_registry_find_interface(const struct oak_symbol_registry_t* registry,
                               const char* name)
{
  const struct oak_symbol_t* s = oak_symbol_registry_find(registry, name);
  if (!s || s->kind != OAK_SYMBOL_INTERFACE)
    return null;
  return &registry->interfaces[s->payload_index];
}
