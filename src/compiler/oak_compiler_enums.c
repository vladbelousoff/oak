#include "internal/oak_compiler.h"
#include "oak_bind.h"

#include <string.h>

/* ---------- oak_enum_registry_t lifecycle ---------- */

void oak_enum_registry_init(struct oak_enum_registry_t* r,
                            struct oak_allocator_t* allocator)
{
  r->allocator = allocator;
  oak_htable_init(&r->by_name, allocator);
  oak_htable_init(&r->enum_names, allocator);
  oak_dynarr_init(
      &r->variants.items, &r->variants.count, &r->variants.capacity);
  oak_dynarr_init(&r->enums.items, &r->enums.count, &r->enums.capacity);
}

void oak_enum_registry_free(struct oak_enum_registry_t* r)
{
  for (int i = 0; i < r->enums.count; ++i)
  {
    struct oak_registered_enum_t* e = &r->enums.items[i];
    if (e->attrs)
      OAK_FREE(r->allocator, e->attrs);
  }
  oak_htable_free(&r->by_name);
  oak_htable_free(&r->enum_names);
  oak_dynarr_free(r->allocator,
      &r->variants.items, &r->variants.count, &r->variants.capacity);
  oak_dynarr_free(r->allocator, &r->enums.items, &r->enums.count, &r->enums.capacity);
}

const struct oak_registered_enum_t* oakc_enum_find(
    const struct oak_enum_registry_t* r, const char* name)
{
  for (int i = 0; i < r->enums.count; ++i)
  {
    if (strcmp(r->enums.items[i].name, name) == 0)
      return &r->enums.items[i];
  }
  return null;
}

struct oak_enum_variant_t*
oak_enum_registry_insert(struct oak_enum_registry_t* r,
                         const struct oak_enum_variant_t* v)
{
  oak_dynarr_push(r->allocator, &r->variants.items,
                  &r->variants.count,
                  &r->variants.capacity,
                  v,
                  sizeof(*v));
  const int idx = r->variants.count - 1;

  /* Index by unqualified variant name (first-wins for unqualified lookup;
   * qualified lookup uses a linear scan and always works). */
  if (oak_htable_get(&r->by_name,
                     r->variants.items[idx].name,
                     r->variants.items[idx].name_len) < 0)
  {
    oak_htable_insert(&r->by_name,
                      r->variants.items[idx].name,
                      r->variants.items[idx].name_len,
                      idx);
  }

  /* Index the enum type name as a set entry (value 1) if not already present.
   */
  if (oak_htable_get(&r->enum_names,
                     r->variants.items[idx].enum_name,
                     r->variants.items[idx].enum_name_len) < 0)
  {
    oak_htable_insert(&r->enum_names,
                      r->variants.items[idx].enum_name,
                      r->variants.items[idx].enum_name_len,
                      1);
  }

  return &r->variants.items[idx];
}

const struct oak_enum_variant_t* oak_enum_registry_find(
    const struct oak_enum_registry_t* r, const char* name, usize len)
{
  const int idx = oak_htable_get(&r->by_name, name, len);
  if (idx < 0)
    return null;
  return &r->variants.items[idx];
}

const struct oak_enum_variant_t*
oakc_enums_find_qualified(const struct oak_enum_registry_t* r,
                                 const char* enum_name,
                                 const char* variant_name)
{
  /* Linear scan: qualified lookup is rare (only EnumName.Variant expressions).
   */
  for (int i = 0; i < r->variants.count; ++i)
  {
    const struct oak_enum_variant_t* v = &r->variants.items[i];
    if (strcmp(v->enum_name, enum_name) == 0 &&
        strcmp(v->name, variant_name) == 0)
      return v;
  }
  return null;
}

int oakc_is_enum_name(const struct oak_enum_registry_t* r,
                                   const char* name,
                                   usize len)
{
  return oak_htable_get(&r->enum_names, name, len) >= 0;
}


void oakc_register_native_enums(
    struct oak_compiler_t* c, const struct oak_compile_options_t* opts)
{
  if (!opts || opts->native_enums.count == 0)
    return;

  for (int i = 0; i < opts->native_enums.count; ++i)
  {
    const struct oak_bind_enum_t* ne = opts->native_enums.items[i];
    if (!ne)
      continue;
    if (ne->module_name)
      continue;

    if (oakc_is_enum_name(&c->enums, ne->name, ne->name_len))
    {
      oak_compiler_error_at(
          c,
          null,
          "native enum '%s' conflicts with an already-registered enum",
          ne->name);
      return;
    }

    const oak_type_id_t enum_type_id =
        oak_type_registry_intern(&c->types, ne->name, ne->name_len);
    if (enum_type_id < 0)
    {
      oak_compiler_error_at(
          c, null, "failed to register native enum '%s' as a type", ne->name);
      return;
    }

    {
      struct oak_registered_enum_t re = {
        .name = ne->name,
        .name_len = ne->name_len,
        .type_id = enum_type_id,
        .attrs = null,
        .attr_count = 0,
      };
      oak_dynarr_push(c->allocator, &c->enums.enums.items,
                      &c->enums.enums.count,
                      &c->enums.enums.capacity,
                      &re,
                      sizeof(re));
    }

    for (int vi = 0; vi < ne->variant_count; ++vi)
    {
      const struct oak_bind_enum_variant_t* nv = &ne->variants[vi];

      if (oak_enum_registry_find(&c->enums, nv->name, nv->name_len))
      {
        oak_compiler_error_at(
            c,
            null,
            "native enum variant '%s' conflicts with an already-registered "
            "variant",
            nv->name);
        return;
      }

      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(nv->value));
      if (c->has_error)
        return;

      struct oak_enum_variant_t v = {
        .name = nv->name,
        .name_len = nv->name_len,
        .enum_name = ne->name,
        .enum_name_len = ne->name_len,
        .const_idx = idx,
        .value = nv->value,
        .type_id = enum_type_id,
      };
      oak_enum_registry_insert(&c->enums, &v);
    }
  }
}

void oakc_register_program_enums(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* program)
{
  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const struct oak_ast_node_t* raw_item =
        oak_container_of(pos, struct oak_ast_node_t, link);
    const struct oak_ast_node_t* item = oakc_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_ENUM_DECL)
      continue;

    /* ENUM_DECL is BINARY: lhs = IDENT (name), rhs = ENUM_VARIANTS. */
    const struct oak_ast_node_t* name_node = item->lhs;
    const struct oak_ast_node_t* variants_node = item->rhs;
    if (!name_node || !variants_node)
    {
      oak_compiler_error_at(c, item->token, "malformed enum declaration");
      return;
    }

    const char* enum_name_check = oak_token_text(name_node->token);
    const usize enum_name_check_len = oak_token_size(name_node->token);
    if (oakc_is_enum_name(&c->enums, enum_name_check, enum_name_check_len))
    {
      oak_compiler_error_at(
          c, name_node->token, "enum '%s' conflicts with an imported enum",
          enum_name_check);
      return;
    }

    const oak_type_id_t enum_type_id =
        oakc_intern_type_tok(c, name_node->token);
    if (enum_type_id < 0)
    {
      oak_compiler_error_at(
          c, name_node->token, "failed to register enum as a type");
      return;
    }

    /* Register enum-level metadata (name, type_id, attributes). */
    {
      int attr_count = 0;
      const char** attrs = oakc_extract_attrs(c->allocator, raw_item, &attr_count);
      const char* enum_name = oak_token_text(name_node->token);
      oakc_dispatch_compile_attr_cbs(
          c, attrs, attr_count, enum_name, OAK_ATTR_TARGET_ENUM,
          null, 0, null, 0, -1);
      struct oak_registered_enum_t re = {
        .name = enum_name,
        .name_len = oak_token_size(name_node->token),
        .type_id = enum_type_id,
        .attrs = attrs,
        .attr_count = attr_count,
      };
      oak_dynarr_push(c->allocator, &c->enums.enums.items,
                      &c->enums.enums.count,
                      &c->enums.enums.capacity,
                      &re,
                      sizeof(re));
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
      const usize vname_len = oak_token_size(variant->token);

      /* Duplicate variant name check (across all enums). */
      if (oak_enum_registry_find(&c->enums, vname, vname_len))
      {
        oak_compiler_error_at(
            c, variant->token, "duplicate enum variant '%s'", vname);
        return;
      }

      /* Store the integer value as a chunk constant. */
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(ordinal));
      if (c->has_error)
        return;

      struct oak_enum_variant_t v = {
        .name = vname,
        .name_len = vname_len,
        .enum_name = oak_token_text(name_node->token),
        .enum_name_len = oak_token_size(name_node->token),
        .const_idx = idx,
        .value = ordinal,
        .type_id = enum_type_id,
      };
      oak_enum_registry_insert(&c->enums, &v);
      ++ordinal;
    }
  }
}
