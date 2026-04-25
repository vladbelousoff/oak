#include "oak_compiler_internal.h"

#include <string.h>

/* ---------- oak_enum_registry_t lifecycle ---------- */

void oak_enum_registry_init(struct oak_enum_registry_t* r)
{
  oak_hash_table_init(&r->by_name);
  oak_hash_table_init(&r->enum_names);
  r->variants = null;
  r->count = 0;
  r->capacity = 0;
}

void oak_enum_registry_free(struct oak_enum_registry_t* r)
{
  oak_hash_table_free(&r->by_name);
  oak_hash_table_free(&r->enum_names);
  if (r->variants)
    oak_free(r->variants, OAK_SRC_LOC);
  r->variants = null;
  r->count = 0;
  r->capacity = 0;
}

struct oak_enum_variant_t*
oak_enum_registry_insert(struct oak_enum_registry_t* r,
                         const struct oak_enum_variant_t* v)
{
  if (r->count >= r->capacity)
  {
    const int new_cap = r->capacity < 8 ? 8 : r->capacity * 2;
    r->variants = oak_realloc(
        r->variants, (usize)new_cap * sizeof *r->variants, OAK_SRC_LOC);
    r->capacity = new_cap;
  }
  const int idx = r->count;
  r->variants[idx] = *v;
  r->count++;

  /* Index by unqualified variant name. */
  oak_hash_table_insert(
      &r->by_name, r->variants[idx].name, r->variants[idx].name_len, idx);

  /* Index the enum type name as a set entry (value 1) if not already present.
   */
  if (oak_hash_table_get(&r->enum_names,
                         r->variants[idx].enum_name,
                         r->variants[idx].enum_name_len) < 0)
  {
    oak_hash_table_insert(&r->enum_names,
                          r->variants[idx].enum_name,
                          r->variants[idx].enum_name_len,
                          1);
  }

  return &r->variants[idx];
}

const struct oak_enum_variant_t* oak_enum_registry_find(
    const struct oak_enum_registry_t* r, const char* name, usize len)
{
  const int idx = oak_hash_table_get(&r->by_name, name, len);
  if (idx < 0)
    return null;
  return &r->variants[idx];
}

const struct oak_enum_variant_t*
oak_enum_registry_find_qualified(const struct oak_enum_registry_t* r,
                                 const char* enum_name,
                                 usize enum_name_len,
                                 const char* variant_name,
                                 usize variant_name_len)
{
  /* Linear scan: qualified lookup is rare (only EnumName.Variant expressions).
   */
  for (int i = 0; i < r->count; ++i)
  {
    const struct oak_enum_variant_t* v = &r->variants[i];
    if (oak_name_eq(v->enum_name, v->enum_name_len, enum_name, enum_name_len) &&
        oak_name_eq(v->name, v->name_len, variant_name, variant_name_len))
      return v;
  }
  return null;
}

int oak_enum_registry_is_enum_name(const struct oak_enum_registry_t* r,
                                   const char* name,
                                   usize len)
{
  return oak_hash_table_get(&r->enum_names, name, len) >= 0;
}

/* ---------- Compiler-level wrappers ---------- */

const struct oak_enum_variant_t* oak_compiler_find_enum_variant(
    const struct oak_compiler_t* c, const char* name, const usize len)
{
  return oak_enum_registry_find(&c->enums, name, len);
}

const struct oak_enum_variant_t*
oak_compiler_find_enum_variant_qualified(const struct oak_compiler_t* c,
                                         const char* enum_name,
                                         const usize enum_name_len,
                                         const char* variant_name,
                                         const usize variant_name_len)
{
  return oak_enum_registry_find_qualified(
      &c->enums, enum_name, enum_name_len, variant_name, variant_name_len);
}

int oak_compiler_is_enum_name(const struct oak_compiler_t* c,
                              const char* name,
                              const usize len)
{
  return oak_enum_registry_is_enum_name(&c->enums, name, len);
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

      /* Store the integer value as a chunk constant. */
      const u8 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(ordinal));
      if (c->has_error)
        return;

      struct oak_enum_variant_t v = {
        .name = vname,
        .name_len = vname_len,
        .enum_name = oak_token_text(name_node->token),
        .enum_name_len = oak_token_length(name_node->token),
        .const_idx = idx,
        .value = ordinal,
      };
      oak_enum_registry_insert(&c->enums, &v);
      ++ordinal;
    }
  }
}
