#include "internal/oak_compiler.h"
#include "oak_bind.h"

#include <string.h>


void oak_enum_registry_init(oak_enum_registry_t* r,
                            oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->enum_names = oak_hash_set_new(allocator);
  r->variants = oak_vector_new(allocator, sizeof(oak_enum_variant_t));
  r->enums = oak_vector_new(allocator, sizeof(oak_registered_enum_t));
  OAK_ASSERT(r->enum_names && r->variants && r->enums);
}

void oak_enum_registry_free(oak_enum_registry_t* r)
{
  oak_registered_enum_t* enums =
      OAK_DATA(oak_registered_enum_t, r->enums);
  for (usize i = 0; i < oak_size(r->enums); ++i)
  {
    if (enums[i].attrs)
      oak_free(r->allocator, enums[i].attrs, OAK_HERE);
  }
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
  return OAK_NULL;
}

oak_enum_variant_t*
oak_enum_registry_insert(oak_enum_registry_t* r,
                         const oak_enum_variant_t* v)
{
  OAK_ASSERT(oak_push_back(r->variants, v));
  oak_enum_variant_t* variant =
      oak_get(r->variants, oak_size(r->variants) - 1);

  /* Record the enum type name as a set member if not already present. */
  oak_add_str(r->enum_names, variant->enum_name);

  return variant;
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
  return OAK_NULL;
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
    if (ne->module)
      continue;

    if (oak_is_enum_name(&c->enums, ne->name))
    {
      oak_compiler_error_at(
          c,
          OAK_NULL,
          "native enum '%s' conflicts with an already-registered enum",
          ne->name);
      return;
    }

    const oak_type_id_t enum_type_id =
        oak_type_registry_intern(&c->types, ne->name);
    if (enum_type_id < 0)
    {
      oak_compiler_error_at(
          c, OAK_NULL, "failed to register native enum '%s' as a type", ne->name);
      return;
    }
    /* Published on the descriptor so OAK_BIND_ENUM refs can resolve to it. */
    ne->resolved_type_id = enum_type_id;

    {
      oak_registered_enum_t re = {
        .name = ne->name,
        .type_id = enum_type_id,
        .source_module_id = OAK_MODULE_ID_NONE,
        .attrs = OAK_NULL,
        .attr_count = 0,
      };
      if (!oak_compiler_declare_symbol(
              c, OAK_NULL, re.name, OAK_SYMBOL_ENUM,
              (int)oak_size(c->enums.enums), OAK_MODULE_ID_NONE, 0))
        return;
      OAK_ASSERT(oak_push_back(c->enums.enums, &re));
    }

    const oak_bind_enum_variant_t* bind_variants =
        OAK_CDATA(oak_bind_enum_variant_t, ne->variants);
    for (usize vi = 0; vi < oak_size(ne->variants); ++vi)
    {
      const oak_bind_enum_variant_t* nv = &bind_variants[vi];

      if (oak_enums_find_qualified(&c->enums, ne->name, nv->name))
      {
        oak_compiler_error_at(
            c,
            OAK_NULL,
            "native enum '%s' declares variant '%s' twice",
            ne->name,
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

/* The native enum descriptor a source `enum` declaration in this compilation
 * unit is the stub for, or NULL when the declaration stands on its own.
 *
 * Matching is by name *and* by module: a descriptor registered with
 * oak_bind_enum(opts, raylib, "Key") backs the `Key` declared in
 * raylib.oak and nothing else.  A descriptor with no module (oak_bind_enum)
 * never matches a source declaration -- oak_register_native_enums creates that
 * enum itself, and would already have rejected a source enum of the same name
 * as a conflict. */
static const oak_bind_enum_t* native_enum_backing(const oak_compiler_t* c,
                                                  const char* name)
{
  const char* dotted =
      c->current_module ? c->current_module->dotted_name : OAK_NULL;
  if (!c->opts || !dotted)
    return OAK_NULL;

  oak_bind_enum_t** enums = OAK_DATA(oak_bind_enum_t*, c->opts->native_enums);
  for (usize i = 0; i < oak_size(c->opts->native_enums); ++i)
  {
    const oak_bind_enum_t* e = enums[i];
    const char* e_module = e ? oak_bind_module_name(e->module) : OAK_NULL;
    if (!e_module)
      continue;
    if (strcmp(e_module, dotted) == 0 && strcmp(e->name, name) == 0)
      return e;
  }
  return OAK_NULL;
}

/* The bound value for `variant_name`, or 0 with *found cleared. */
static int native_enum_variant_value(const oak_bind_enum_t* e,
                                     const char* variant_name,
                                     int* found)
{
  const oak_bind_enum_variant_t* variants =
      OAK_CDATA(oak_bind_enum_variant_t, e->variants);
  for (usize i = 0; i < oak_size(e->variants); ++i)
  {
    if (strcmp(variants[i].name, variant_name) == 0)
    {
      *found = 1;
      return variants[i].value;
    }
  }
  *found = 0;
  return 0;
}

void oak_register_program_enums(oak_compiler_t* c,
                                         const oak_ast_node_t* program)
{
  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &program->children)
  {
    const oak_ast_node_t* raw_item =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
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
          OAK_NULL, 0, OAK_NULL, 0, -1);
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
      OAK_ASSERT(oak_push_back(c->enums.enums, &re));
    }

    /* When a native binding backs this declaration, the binding owns the
     * variant *values* and the declaration owns the variant *names* -- the
     * same split the stub already has with functions and methods, where the
     * declaration states the signature and the binding supplies the code.
     * Every variant must appear on both sides; see the checks below. */
    const oak_bind_enum_t* backing =
        native_enum_backing(c, oak_token_text(name_node->token));
    usize declared_variants = 0;

    int ordinal = 0;
    oak_list_entry_t* vpos;
    OAK_LIST_FOR_EACH(vpos, &variants_node->children)
    {
      const oak_ast_node_t* variant =
          OAK_CONTAINER_OF(vpos, oak_ast_node_t, link);
      if (variant->kind != OAK_NODE_IDENT)
        continue;

      const char* vname = oak_token_text(variant->token);

      /* Within this enum only. Two enums sharing a variant name is not a
       * conflict: every reference is qualified, so `Key.Left` and
       * `MouseButton.Left` name different things and always did. */
      if (oak_enums_find_qualified(
              &c->enums, oak_token_text(name_node->token), vname))
      {
        oak_compiler_error_at(
            c, variant->token, "duplicate enum variant '%s'", vname);
        return;
      }

      int value = ordinal;
      if (backing)
      {
        int found = 0;
        value = native_enum_variant_value(backing, vname, &found);
        if (!found)
        {
          oak_compiler_error_at(
              c, variant->token,
              "enum '%s' declares variant '%s' with no native binding",
              oak_token_text(name_node->token), vname);
          return;
        }
        ++declared_variants;
      }

      /* Store the integer value as a chunk constant. */
      const u16 idx = oak_compiler_intern_constant(c, OAK_VALUE_I32(value));
      if (c->has_error)
        return;

      oak_enum_variant_t v = {
        .name = vname,
        .enum_name = oak_token_text(name_node->token),
        .const_idx = idx,
        .value = value,
        .type_id = enum_type_id,
      };
      oak_enum_registry_insert(&c->enums, &v);
      ++ordinal;
    }

    /* The other direction: a bound variant the declaration never mentions is
     * unreachable from Oak, which is drift rather than a design choice. Naming
     * one of the missing variants is enough to find the edit that caused it. */
    if (backing && declared_variants < oak_size(backing->variants))
    {
      const oak_bind_enum_variant_t* variants =
          OAK_CDATA(oak_bind_enum_variant_t, backing->variants);
      for (usize i = 0; i < oak_size(backing->variants); ++i)
      {
        if (!oak_enums_find_qualified(
                &c->enums, oak_token_text(name_node->token), variants[i].name))
        {
          oak_compiler_error_at(
              c, name_node->token,
              "native enum '%s' binds variant '%s' that the declaration is "
              "missing",
              oak_token_text(name_node->token), variants[i].name);
          return;
        }
      }
    }
  }
}
