#include "internal/oak_compiler.h"

int oak_compiler_declare_symbol(oak_compiler_t* c,
                                const oak_token_t* token,
                                const char* name,
                                oak_symbol_kind_t kind,
                                int payload_index,
                                u16 owner_module_id,
                                int is_imported)
{
  const oak_symbol_t* existing =
      oak_symbol_registry_find(&c->symbols, name);
  if (existing)
  {
    oak_compiler_error_at(c, token, "duplicate top-level symbol '%s'", name);
    return 0;
  }
  oak_symbol_t symbol = {
    .name = name,
    .kind = kind,
    .owner_module_id = owner_module_id,
    .payload_index = payload_index,
    .is_exported = 0,
    .is_imported = is_imported,
  };
  OAK_ASSERT(oak_symbol_registry_insert(&c->symbols, &symbol));
  return 1;
}

void oak_compiler_mark_symbol_exported(oak_compiler_t* c,
                                       const char* name)
{
  oak_symbol_t* symbol =
      (oak_symbol_t*)oak_symbol_registry_find(&c->symbols, name);
  if (!symbol || symbol->is_imported ||
      symbol->owner_module_id == OAK_MODULE_ID_NONE ||
      symbol->kind == OAK_SYMBOL_GLOBAL ||
      symbol->kind == OAK_SYMBOL_MODULE_ALIAS)
    return;
  symbol->is_exported = 1;
}
