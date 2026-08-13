#include "internal/oak_compiler.h"
#include "oak_bind.h"

#include <string.h>


void oak_enum_registry_init(oak_enum_registry_t* r,
                            oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->by_name = oak_hash_map_new(allocator, sizeof(usize));
  r->enum_names = oak_hash_set_new(allocator);
  r->variants = oak_vector_new(allocator, sizeof(oak_enum_variant_t));
  r->enums = oak_vector_new(allocator, sizeof(oak_registered_enum_t));
  oak_assert(r->by_name && r->enum_names && r->variants && r->enums);
}

void oak_enum_registry_free(oak_enum_registry_t* r)
{
  oak_registered_enum_t* enums =
      OAK_DATA(oak_registered_enum_t, r->enums);
  for (usize i = 0; i < oak_size(r->enums); ++i)
  {
    if (enums[i].attrs)
      OAK_FREE(r->allocator, enums[i].attrs);
  }
  oak_destroy(r->by_name);
  oak_destroy(r->enum_names);
  oak_destroy(r->variants);
  oak_destroy(r->enums);
}

const oak_registered_enum_t* oak_enum_find(
    const oak_enum_registry_t* r, const char* name)
{
  const oak_registered_enum_t* enums =
      OAK_CDATA(oak_registered_enum_t, r->enums);
  for (usize i = 0; i < oak_size(r->enums); ++i)
  {
    if (strcmp(enums[i].name, name) == 0)
      return &enums[i];
  }
  return null;
}

oak_enum_variant_t*
oak_enum_registry_insert(oak_enum_registry_t* r,
                         const oak_enum_variant_t* v)
{
  oak_assert(oak_push_back(r->variants, v));
  const usize idx = oak_size(r->variants) - 1;
  oak_enum_variant_t* variant = oak_get(r->variants, idx);

  /* Index by unqualified variant name (first-wins for unqualified lookup;
   * qualified lookup uses a linear scan and always works). */
  if (!oak_contains_str(r->by_name, variant->name))
    oak_assert(oak_put_str(r->by_name, variant->name, &idx));

  /* Record the enum type name as a set member if not already present. */
  oak_add_str(r->enum_names, variant->enum_name);

  return variant;
}

const oak_enum_variant_t* oak_enum_registry_find(
    const oak_enum_registry_t* r, const char* name)
{
  const usize* idx = oak_cfind_str(r->by_name, name);
  return idx ? oak_cget(r->variants, *idx) : null;
}

const oak_enum_variant_t*
oak_enums_find_qualified(const oak_enum_registry_t* r,
                                 const char* enum_name,
                                 const char* variant_name)
{
  /* Linear scan: qualified lookup is rare (only EnumName.Variant expressions).
   */
  const oak_enum_variant_t* variants =
      OAK_CDATA(oak_enum_variant_t, r->variants);
  for (usize i = 0; i < oak_size(r->variants); ++i)
  {
    const oak_enum_variant_t* v = &variants[i];
    if (strcmp(v->enum_name, enum_name) == 0 &&
        strcmp(v->name, variant_name) == 0)
      return v;
  }
  return null;
}

int oak_is_enum_name(const oak_enum_registry_t* r, const char* name)
{
  return oak_contains_str(r->enum_names, name);
}


void oak_register_native_enums(
    oak_compiler_t* c, const oak_compile_options_t* opts)
{
  if (!opts || oak_size(opts->native_enums) == 0)
    return;

  oak_bind_enum_t** native_enums =
      OAK_DATA(oak_bind_enum_t*, opts->native_enums);
  for (usize i = 0; i < oak_size(opts->native_enums); ++i)
  {
    oak_bind_enum_t* ne = native_enums[i];
    if (!ne)
      continue;
    if (ne->module_name)
      continue;

    if (oak_is_enum_name(&c->enums, ne->name))
    {
      oak_compiler_error_at(
          c,
          null,
          "native enum '%s' conflicts with an already-registered enum",
          ne->name);
      return;
    }

    const oak_type_id_t enum_type_id =
        oak_type_registry_intern(&c->types, ne->name);
    if (enum_type_id < 0)
    {
      oak_compiler_error_at(
          c, null, "failed to register native enum '%s' as a type", ne->name);
      return;
    }
    /* Published on the descriptor so OAK_BIND_ENUM refs can resolve to it. */
    ne->resolved_type_id = enum_type_id;

    {
      oak_registered_enum_t re = {
        .name = ne->name,
        .type_id = enum_type_id,
        .source_module_id = OAK_MODULE_ID_NONE,
        .attrs = null,
        .attr_count = 0,
      };
      if (!oak_compiler_declare_symbol(
              c, null, re.name, OAK_SYMBOL_ENUM,
              (int)oak_size(c->enums.enums), OAK_MODULE_ID_NONE, 0))
        return;
      oak_assert(oak_push_back(c->enums.enums, &re));
    }

    const oak_bind_enum_variant_t* bind_variants =
        OAK_CDATA(oak_bind_enum_variant_t, ne->variants);
    for (usize vi = 0; vi < oak_size(ne->variants); ++vi)
    {
      const oak_bind_enum_variant_t* nv = &bind_variants[vi];

      if (oak_enum_registry_find(&c->enums, nv->name))
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

      oak_enum_variant_t v = {
        .name = nv->name,
        .enum_name = ne->name,
        .const_idx = idx,
        .value = nv->value,
        .type_id = enum_type_id,
      };
      oak_enum_registry_insert(&c->enums, &v);
    }
  }
}

void oak_register_program_enums(oak_compiler_t* c,
                                         const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        oak_container_of(pos, oak_ast_node_t, link);
    const oak_ast_node_t* item = oak_unwrap_decl(raw_item);
    if (!item || item->kind != OAK_NODE_ENUM_DECL)
      continue;

    /* ENUM_DECL is BINARY: lhs = IDENT (name), rhs = ENUM_VARIANTS. */
    const oak_ast_node_t* name_node = item->lhs;
    const oak_ast_node_t* variants_node = item->rhs;
    if (!name_node || !variants_node)
    {
      oak_compiler_error_at(c, item->token, "malformed enum declaration");
      return;
    }

    const char* enum_name_check = oak_token_text(name_node->token);
    if (oak_is_enum_name(&c->enums, enum_name_check))
    {
      oak_compiler_error_at(
          c, name_node->token, "enum '%s' conflicts with an imported enum",
          enum_name_check);
      return;
    }

    const oak_type_id_t enum_type_id =
        oak_intern_type_tok(c, name_node->token);
    if (enum_type_id < 0)
    {
      oak_compiler_error_at(
          c, name_node->token, "failed to register enum as a type");
      return;
    }

    /* Register enum-level metadata (name, type_id, attributes). */
    {
      int attr_count = 0;
      const char** attrs = oak_extract_attrs(c->allocator, raw_item, &attr_count);
      const char* enum_name = oak_token_text(name_node->token);
      oak_compiler_dispatch_attr_cbs(
          c, attrs, attr_count, enum_name, OAK_ATTR_TARGET_ENUM,
          null, 0, null, 0, -1);
      oak_registered_enum_t re = {
        .name = enum_name,
        .type_id = enum_type_id,
        .attrs = attrs,
        .attr_count = attr_count,
      };
      const u16 owner_module_id =
          c->current_module ? c->current_module->module_id : OAK_MODULE_ID_NONE;
      if (!oak_compiler_declare_symbol(
              c, name_node->token, re.name, OAK_SYMBOL_ENUM,
              (int)oak_size(c->enums.enums), owner_module_id, 0))
        return;
      if (oak_decl_is_exported(raw_item))
        oak_compiler_mark_symbol_exported(c, re.name);
      oak_assert(oak_push_back(c->enums.enums, &re));
    }

    int ordinal = 0;
    oak_list_entry_t* vpos;
    oak_list_for_each(vpos, &variants_node->children)
    {
      const oak_ast_node_t* variant =
          oak_container_of(vpos, oak_ast_node_t, link);
      if (variant->kind != OAK_NODE_IDENT)
        continue;

      const char* vname = oak_token_text(variant->token);

      /* Duplicate variant name check (across all enums). */
      if (oak_enum_registry_find(&c->enums, vname))
      {
        oak_compiler_error_at(
            c, variant->token, "duplicate enum variant '%s'", vname);
        return;
      }

      /* Store the integer value as a chunk constant. */
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(ordinal));
      if (c->has_error)
        return;

      oak_enum_variant_t v = {
        .name = vname,
        .enum_name = oak_token_text(name_node->token),
        .const_idx = idx,
        .value = ordinal,
        .type_id = enum_type_id,
      };
      oak_enum_registry_insert(&c->enums, &v);
      ++ordinal;
    }
  }
}
