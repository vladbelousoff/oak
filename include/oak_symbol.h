#pragma once

#include "oak_htable.h"
#include "oak_types.h"

struct oak_allocator_t;

enum oak_symbol_kind_t
{
  OAK_SYMBOL_FUNCTION,
  OAK_SYMBOL_RECORD,
  OAK_SYMBOL_ENUM,
  OAK_SYMBOL_TRAIT,
  OAK_SYMBOL_GLOBAL,
  OAK_SYMBOL_MODULE_ALIAS,
};

struct oak_symbol_t
{
  const char* name;
  int name_len;
  enum oak_symbol_kind_t kind;
  u16 owner_module_id;
  int payload_index;
  int is_exported;
  int is_imported;
};

struct oak_symbol_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_htable_t by_name;
  struct oak_symbol_t* symbols;
};

void oak_symbol_registry_init(struct oak_symbol_registry_t* registry,
                              struct oak_allocator_t* allocator);
void oak_symbol_registry_free(struct oak_symbol_registry_t* registry);

/* Inserts a symbol into the module namespace. Returns null on collision. */
struct oak_symbol_t* oak_symbol_registry_insert(
    struct oak_symbol_registry_t* registry, const struct oak_symbol_t* symbol);

const struct oak_symbol_t* oak_symbol_registry_find(
    const struct oak_symbol_registry_t* registry, const char* name, int len);
