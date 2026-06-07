#include "internal/oak_compiler.h"

int oak_compiler_declare_symbol(struct oak_compiler_t* c,
                                const struct oak_token_t* token,
                                const char* name,
                                int name_len,
                                enum oak_symbol_kind_t kind,
                                int payload_index,
                                u16 owner_module_id,
                                int is_imported)
{
  const struct oak_symbol_t* existing =
      oak_symbol_registry_find(&c->symbols, name, name_len);
  if (existing)
  {
    oak_compiler_error_at(
        c, token, "duplicate top-level symbol '%.*s'", name_len, name);
    return 0;
  }
  struct oak_symbol_t symbol = {
    .name = name,
    .name_len = name_len,
    .kind = kind,
    .owner_module_id = owner_module_id,
    .payload_index = payload_index,
    .is_exported = !is_imported && owner_module_id != OAK_MODULE_ID_NONE &&
                   kind != OAK_SYMBOL_GLOBAL &&
                   kind != OAK_SYMBOL_MODULE_ALIAS,
    .is_imported = is_imported,
  };
  oak_assert(oak_symbol_registry_insert(&c->symbols, &symbol));
  return 1;
}
