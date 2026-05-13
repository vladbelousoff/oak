#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

void oak_compiler_compile_record_literal(struct oak_compiler_t* c,
                                         const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* path_node = node->lhs;
  const struct oak_ast_node_t* fields_node = node->rhs;
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
  const struct oak_ast_node_t* seg[2] = { null, null };
  const int seg_count = oak_compiler_import_path_segments(path_node, seg, 2);
  if (seg_count < 1 || seg_count > 2)
  {
    oak_compiler_error_at(
        c,
        node->token,
        "record literal: type path must be 'Type' or 'mod.Type'");
    return;
  }

  const struct oak_ast_node_t* type_seg = seg[seg_count - 1]; /* last segment */
  const char* sname = oak_token_text(type_seg->token);
  const usize sname_len = oak_token_length(type_seg->token);

  /* For a qualified path (mod.Type) verify the module alias is valid. */
  if (seg_count == 2)
  {
    const char* alias = oak_token_text(seg[0]->token);
    const usize alias_len = oak_token_length(seg[0]->token);
    if (!oak_compiler_module_for_alias(c, alias, alias_len))
    {
      oak_compiler_error_at(
          c, seg[0]->token, "'%s' is not an imported module", alias);
      return;
    }
  }

  const struct oak_ast_node_t* name_node = type_seg;

  const struct oak_registered_record_t* sd =
      oakc_records_find(&c->records, sname, sname_len);
  if (!sd)
  {
    oak_compiler_error_at(
        c, type_seg->token, "unknown record type '%s'", sname);
    return;
  }

  const struct oak_ast_node_t** exprs =
      oak_alloc((usize)sd->field_count * sizeof(*exprs), OAK_SRC_LOC);
  for (int i = 0; i < sd->field_count; ++i)
    exprs[i] = null;

  struct oak_list_entry_t* pos;
  oak_list_for_each(pos, &fields_node->children)
  {
    const struct oak_ast_node_t* entry =
        oak_container_of(pos, struct oak_ast_node_t, link);
    if (entry->kind != OAK_NODE_RECORD_LITERAL_FIELD || !entry->lhs)
    {
      oak_compiler_error_at(
          c, entry->token, "malformed record field initializer");
      goto cleanup_exprs;
    }
    const struct oak_ast_node_t* fname = entry->lhs;
    /* Shorthand `{ foo }` desugars to `{ foo: foo }` — use the name node as
     * the value expression so compile_node emits a GET_LOCAL. */
    const struct oak_ast_node_t* fexpr = entry->rhs ? entry->rhs : fname;
    if (fname->kind != OAK_NODE_IDENT)
    {
      oak_compiler_error_at(
          c, fname->token, "record field name must be an identifier");
      goto cleanup_exprs;
    }

    const usize fname_len = oak_token_length(fname->token);
    const int idx = oakc_record_field(
        sd, oak_token_text(fname->token), fname_len);
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

    struct oak_type_t got;
    oakc_infer_type(c, fexpr, &got);
    if (oak_type_is_known(&got) &&
        !oakc_type_accepts(&sd->fields[idx].type, &got))
    {
      oak_compiler_error_at(
          c,
          fexpr->token ? fexpr->token : fname->token,
          "field '%s': expected type '%s', got '%s'",
          sd->fields[idx].name,
          oakc_type_full_name(c, sd->fields[idx].type),
          oakc_type_full_name(c, got));
      goto cleanup_exprs;
    }

    exprs[idx] = fexpr;
  }

  for (int i = 0; i < sd->field_count; ++i)
  {
    if (!exprs[i])
    {
      oak_compiler_error_at(c,
                            name_node->token,
                            "missing field '%s' in '%s' literal",
                            sd->fields[i].name,
                            sd->name);
      goto cleanup_exprs;
    }
  }

  {
    const char** fptr =
        oak_alloc((usize)sd->field_count * sizeof(*fptr), OAK_SRC_LOC);
    usize* flen = oak_alloc((usize)sd->field_count * sizeof(*flen), OAK_SRC_LOC);
    for (int i = 0; i < sd->field_count; ++i)
    {
      fptr[i] = sd->fields[i].name;
      flen[i] = sd->fields[i].name_len;
    }
    const int layout_id =
        oak_chunk_add_field_layout(c->chunk, sd->field_count, fptr, flen);
    if (layout_id < 0)
    {
      oak_compiler_error_at(
          c, name_node->token, "internal error: could not add record layout");
      if (fptr)
        oak_free(fptr, OAK_SRC_LOC);
      if (flen)
        oak_free(flen, OAK_SRC_LOC);
      goto cleanup_exprs;
    }
    if (fptr)
      oak_free(fptr, OAK_SRC_LOC);
    if (flen)
      oak_free(flen, OAK_SRC_LOC);

    struct oak_obj_string_t* type_name_obj =
        oak_string_new(sd->name, sd->name_len);
    const u16 name_idx =
        oak_compiler_intern_constant(c, OAK_VALUE_OBJ(type_name_obj));
    oak_compiler_emit_constant(
        c, name_idx, oak_compiler_loc_from_token(name_node->token));

    for (int i = 0; i < sd->field_count; ++i)
    {
      oak_compiler_compile_node(c, exprs[i]);
      if (c->has_error)
        goto cleanup_exprs;
      oakc_emit_trait_coerce(c,
                             exprs[i],
                             sd->fields[i].type,
                             OAK_LOC_SYNTHETIC);
      if (c->has_error)
        goto cleanup_exprs;
      oakc_emit_weak_coerce(c,
                            exprs[i],
                            sd->fields[i].type,
                            OAK_LOC_SYNTHETIC);
      if (c->has_error)
        goto cleanup_exprs;
    }

    oak_compiler_emit_op(c,
                         OAK_OP_NEW_RECORD,
                         OAK_LOC_SYNTHETIC,
                         OAK_ARG_U8((u8)sd->field_count),
                         OAK_ARG_U16((u16)layout_id));
    c->scope.stack_depth -= sd->field_count;
  }

cleanup_exprs:
  if (exprs)
    oak_free(exprs, OAK_SRC_LOC);
}
