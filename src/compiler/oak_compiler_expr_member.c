#include "internal/oak_compiler.h"
#include "internal/oak_compiler_modules.h"

void oak_compiler_compile_member_access(oak_compiler_t* c,
                                        const oak_ast_node_t* node)
{
  const oak_ast_node_t* recv = node->lhs;
  const oak_ast_node_t* fname = node->rhs;
  if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, node->token, "field access requires the form 'expr.field'");
    return;
  }

  /* Cross-module enum variant: alias.EnumName.Variant */
  {
    const oak_token_t* ename_tok = OAK_NULL;
    if (oak_compiler_match_module_member(c, recv, &ename_tok))
    {
      const char* ename = oak_token_text(ename_tok);
      const char* vname = oak_token_text(fname->token);
      const oak_enum_variant_t* ev = oak_enums_find_qualified(
          &c->enums, ename, vname);
      if (ev)
      {
        oak_compiler_emit_constant(
            c, ev->const_idx, oak_compiler_loc_from_token(fname->token));
        return;
      }

      /* Nothing local matched, which is the normal case rather than an error:
       * `import m as a` binds the alias and registers no names, so a.E.V has
       * no entry in c->enums to find. (It resolved before only when the same
       * module was *also* wildcard-imported, which is why this looked like it
       * worked.) The module's export list is the authority. */
      const oak_module_export_enum_t* exp = oak_compiler_module_export_enum(
          c, oak_token_text(recv->lhs->token), ename, OAK_NULL);
      if (exp)
      {
        const oak_module_export_enum_variant_t* variants =
            OAK_CDATA(oak_module_export_enum_variant_t, exp->variants);
        for (usize i = 0; i < oak_size(exp->variants); ++i)
        {
          if (strcmp(variants[i].name, vname) != 0)
            continue;
          /* The importing chunk needs its own constant: the exporting module's
           * constant index means nothing here. */
          const u16 idx =
              oak_compiler_intern_constant(c, OAK_VALUE_I32(variants[i].value));
          if (c->has_error)
            return;
          oak_compiler_emit_constant(
              c, idx, oak_compiler_loc_from_token(fname->token));
          return;
        }
      }

      oak_compiler_error_at(
          c, fname->token, "enum '%s' has no variant '%s'", ename, vname);
      return;
    }
  }

  /* Local enum variant access: EnumName.Variant */
  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* recv_name = oak_token_text(recv->token);
    if (oak_is_enum_name(&c->enums, recv_name))
    {
      const char* vname = oak_token_text(fname->token);
      const oak_enum_variant_t* ev = oak_enums_find_qualified(
          &c->enums, recv_name, vname);
      if (!ev)
      {
        oak_compiler_error_at(c,
                              fname->token,
                              "'%s' is not a variant of enum '%s'",
                              vname,
                              recv_name);
        return;
      }
      oak_compiler_emit_constant(
          c, ev->const_idx, oak_compiler_loc_from_token(fname->token));
      return;
    }
  }

  oak_reject_void(c, recv);
  if (c->has_error)
    return;
  const oak_registered_record_t* sd = OAK_NULL;
  const int idx = oak_require_record_field(c, recv, fname, 0, &sd);
  (void)sd;
  if (idx < 0)
    return;
  oak_compiler_compile_node(c, recv);
  OAK_COMPILER_EMIT_OP(c,
                       OAK_OP_GET_FIELD,
                       oak_compiler_loc_from_token(fname->token),
                       OAK_ARG_U8((u8)idx));
}
