#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

void oak_compiler_compile_record_literal(oak_compiler_t* c,
                                         const oak_ast_node_t* node)
{
  const oak_ast_node_t* path_node = node->lhs;
  const oak_ast_node_t* fields_node = node->rhs;
  if (!path_node || path_node->kind != OAK_NODE_IMPORT_PATH)
  {
    oak_compiler_error_at(
        c, node->token, "record literal: malformed type path");
    return;
  }
  if (!fields_node || fields_node->kind != OAK_NODE_RECORD_LITERAL_FIELDS)
  {
    oak_compiler_error_at(
        c, node->token, "record literal: malformed field list");
    return;
  }

  /* Collect path segments.  1 segment = local type; 2 segments = mod.Type. */
  const oak_ast_node_t* seg[2] = { OAK_NULL, OAK_NULL };
  const int seg_count = oak_compiler_import_path_segments(path_node, seg, 2);
  if (seg_count < 1 || seg_count > 2)
  {
    oak_compiler_error_at(
        c,
        node->token,
        "record literal: type path must be 'Type' or 'mod.Type'");
    return;
  }

  const oak_ast_node_t* type_seg = seg[seg_count - 1]; /* last segment */
  const char* sname = oak_token_text(type_seg->token);

  /* For a qualified path (mod.Type) verify the module alias is valid. */
  if (seg_count == 2)
  {
    const char* alias = oak_token_text(seg[0]->token);
    if (!oak_compiler_module_for_alias(c, alias))
    {
      oak_compiler_error_at(
          c, seg[0]->token, "'%s' is not an imported module", alias);
      return;
    }
  }

  const oak_ast_node_t* name_node = type_seg;

  const oak_registered_record_t* sd =
      oak_records_find(&c->records, sname);
  if (!sd)
  {
    oak_compiler_error_at(
        c, type_seg->token, "unknown record type '%s'", sname);
    return;
  }
  if (sd->is_value)
  {
    oak_compiler_error_at(
        c, type_seg->token,
        "'%s' is a native value type and cannot be constructed with a record "
        "literal; obtain instances from its native functions",
        sname);
    return;
  }

  const oak_record_field_t* fields =
      OAK_CDATA(oak_record_field_t, sd->fields);
  const usize field_count = oak_size(sd->fields);
  const oak_ast_node_t** exprs =
      oak_alloc(c->allocator, field_count * sizeof(*exprs), OAK_HERE);
  for (usize i = 0; i < field_count; ++i)
    exprs[i] = OAK_NULL;

  oak_list_entry_t* pos;
  OAK_LIST_FOR_EACH(pos, &fields_node->children)
  {
    const oak_ast_node_t* entry =
        OAK_CONTAINER_OF(pos, oak_ast_node_t, link);
    if (entry->kind != OAK_NODE_RECORD_LITERAL_FIELD || !entry->lhs)
    {
      oak_compiler_error_at(
          c, entry->token, "malformed record field initializer");
      goto cleanup_exprs;
    }
    const oak_ast_node_t* fname = entry->lhs;
    /* Shorthand `{ foo }` desugars to `{ foo: foo }` — use the name node as
     * the value expression so compile_node emits a GET_LOCAL. */
    const oak_ast_node_t* fexpr = entry->rhs ? entry->rhs : fname;
    if (fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fname->token, "record field name must be an identifier");
      goto cleanup_exprs;
    }

    const int idx = oak_record_field(sd, oak_token_text(fname->token));
    if (idx < 0)
    {
      oak_compiler_error_at(c,
                            fname->token,
                            "no such field '%s' on record '%s'",
                            oak_token_text(fname->token),
                            sd->name);
      goto cleanup_exprs;
    }
    if (exprs[idx])
    {
      oak_compiler_error_at(c,
                            fname->token,
                            "duplicate field '%s' in record literal",
                            oak_token_text(fname->token));
      goto cleanup_exprs;
    }

    oak_type_t got;
    oak_infer_type(c, fexpr, &got);
    if (oak_type_is_known(&got) &&
        !oak_type_accepts(&fields[idx].type, &got))
    {
      oak_compiler_error_at(
          c,
          fexpr->token ? fexpr->token : fname->token,
          "field '%s': expected type '%s', got '%s'",
          fields[idx].name,
          oak_type_full_name(c, fields[idx].type),
          oak_type_full_name(c, got));
      goto cleanup_exprs;
    }

    exprs[idx] = fexpr;
  }

  for (usize i = 0; i < field_count; ++i)
  {
    if (!exprs[i])
    {
      oak_compiler_error_at(c,
                            name_node->token,
                            "missing field '%s' in '%s' literal",
                            fields[i].name,
                            sd->name);
      goto cleanup_exprs;
    }
  }

  {
    const char** fptr =
        oak_alloc(c->allocator, field_count * sizeof(*fptr), OAK_HERE);
    for (usize i = 0; i < field_count; ++i)
      fptr[i] = fields[i].name;
    const int layout_id =
        oak_chunk_add_field_layout(c->chunk, (int)field_count, fptr);
    if (layout_id < 0)
    {
      oak_compiler_error_at(
          c, name_node->token, "internal error: could not add record layout");
      if (fptr)
        oak_free(c->allocator, fptr, OAK_HERE);
      goto cleanup_exprs;
    }
    if (fptr)
      oak_free(c->allocator, fptr, OAK_HERE);

    oak_obj_string_t* type_name_obj =
        oak_string_new_len(c->allocator, sd->name, strlen(sd->name));
    const u16 name_idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(type_name_obj));
    oak_compiler_emit_constant(
        c, name_idx, oak_compiler_loc_from_token(name_node->token));

    for (usize i = 0; i < field_count; ++i)
    {
      oak_compiler_compile_node(c, exprs[i]);
      if (c->has_error)
        goto cleanup_exprs;
      oak_emit_interface_coerce(c,
                             exprs[i],
                             fields[i].type,
                             OAK_LOC_SYNTHETIC);
      if (c->has_error)
        goto cleanup_exprs;
      oak_emit_weak_coerce(c,
                            exprs[i],
                            fields[i].type,
                            OAK_LOC_SYNTHETIC);
      if (c->has_error)
        goto cleanup_exprs;
    }

    OAK_COMPILER_EMIT_OP(c,
                         OAK_OP_NEW_RECORD,
                         OAK_LOC_SYNTHETIC,
                         OAK_ARG_U8((u8)field_count),
                         OAK_ARG_U16((u16)layout_id));
    c->scope.stack_depth -= (int)field_count;
  }

cleanup_exprs:
  if (exprs)
    oak_free(c->allocator, exprs, OAK_HERE);
}
