#include "oak_compiler_internal.h"

#include <string.h>

const struct oak_enum_variant_t*
oak_compiler_find_enum_variant(const struct oak_compiler_t* c,
                               const char* name,
                               const usize len)
{
  for (int i = 0; i < c->enum_variant_count; ++i)
  {
    const struct oak_enum_variant_t* v = &c->enum_variants[i];
    if (oak_name_eq(v->name, v->name_len, name, len))
      return v;
  }
  return null;
}

const struct oak_enum_variant_t*
oak_compiler_find_enum_variant_qualified(const struct oak_compiler_t* c,
                                         const char* enum_name,
                                         const usize enum_name_len,
                                         const char* variant_name,
                                         const usize variant_name_len)
{
  for (int i = 0; i < c->enum_variant_count; ++i)
  {
    const struct oak_enum_variant_t* v = &c->enum_variants[i];
    if (oak_name_eq(v->enum_name, v->enum_name_len, enum_name, enum_name_len) &&
        oak_name_eq(v->name, v->name_len, variant_name, variant_name_len))
      return v;
  }
  return null;
}

int oak_compiler_is_enum_name(const struct oak_compiler_t* c,
                              const char* name,
                              const usize len)
{
  for (int i = 0; i < c->enum_variant_count; ++i)
  {
    const struct oak_enum_variant_t* v = &c->enum_variants[i];
    if (oak_name_eq(v->enum_name, v->enum_name_len, name, len))
      return 1;
  }
  return 0;
}

void oak_compiler_register_program_enums(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (item->kind != OAK_NODE_ENUM_DECL)
      continue;

    /* ENUM_DECL is BINARY: lhs = IDENT (name), rhs = ENUM_VARIANTS. */
    const struct oak_ast_node_t* name_node = item->lhs;
    const struct oak_ast_node_t* variants_node = item->rhs;
    if (!name_node || !variants_node)
    {
      oak_compiler_error_at(c, item->token, "malformed enum declaration");
      return;
    }

    int ordinal = 0;
    struct oak_list_entry_t* vpos;
    oak_list_for_each(vpos, &variants_node->children)
    {
      const struct oak_ast_node_t* variant =
          oak_container_of(vpos, struct oak_ast_node_t, link);
      if (variant->kind != OAK_NODE_IDENT)
        continue;

      const char* vname = oak_token_text(variant->token);
      const usize vname_len = oak_token_length(variant->token);

      /* Duplicate variant name check (across all enums). */
      if (oak_compiler_find_enum_variant(c, vname, vname_len))
      {
        oak_compiler_error_at(
            c, variant->token, "duplicate enum variant '%s'", vname);
        return;
      }

      if (c->enum_variant_count >= OAK_MAX_ENUM_VARIANTS)
      {
        oak_compiler_error_at(c,
                              variant->token,
                              "too many enum variants (max %d)",
                              OAK_MAX_ENUM_VARIANTS);
        return;
      }

      /* Store the integer value as a chunk constant. */
      const u8 idx =
          oak_compiler_intern_constant(c, OAK_VALUE_I32(ordinal));
      if (c->has_error)
        return;

      struct oak_enum_variant_t* slot =
          &c->enum_variants[c->enum_variant_count++];
      slot->name = vname;
      slot->name_len = vname_len;
      slot->enum_name = oak_token_text(name_node->token);
      slot->enum_name_len = oak_token_length(name_node->token);
      slot->const_idx = idx;
      slot->value = ordinal;
      ++ordinal;
    }
  }
}
