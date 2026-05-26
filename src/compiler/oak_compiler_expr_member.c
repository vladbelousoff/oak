#include "internal/oak_compiler.h"
#include "oak_compiler_modules.h"

void oak_compiler_compile_member_access(struct oak_compiler_t* c,
                                        const struct oak_ast_node_t* node)
{
  const struct oak_ast_node_t* recv = node->lhs;
  const struct oak_ast_node_t* fname = node->rhs;
  if (!recv || !fname || fname->kind != OAK_NODE_IDENT)
  {
    oak_compiler_error_at(
        c, node->token, "field access requires the form 'expr.field'");
    return;
  }

  /* Cross-module enum variant: alias.EnumName.Variant */
  {
    const struct oak_token_t* ename_tok = null;
    if (oak_compiler_match_module_member(c, recv, &ename_tok))
    {
      const char* ename = oak_token_text(ename_tok);
      const char* vname = oak_token_text(fname->token);
      const struct oak_enum_variant_t* ev = oakc_enums_find_qualified(
          &c->enums, ename, vname);
      if (!ev)
      {
        oak_compiler_error_at(
            c, fname->token, "enum '%s' has no variant '%s'", ename, vname);
        return;
      }
      oak_compiler_emit_constant(
          c, ev->const_idx, oak_compiler_loc_from_token(fname->token));
      return;
    }
  }

  /* Local enum variant access: EnumName.Variant */
  if (recv->kind == OAK_NODE_IDENT)
  {
    const char* recv_name = oak_token_text(recv->token);
    const usize recv_len = oak_token_size(recv->token);
    if (oakc_is_enum_name(&c->enums, recv_name, recv_len))
    {
      const char* vname = oak_token_text(fname->token);
      const struct oak_enum_variant_t* ev = oakc_enums_find_qualified(
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

  oakc_reject_void(c, recv);
  if (c->has_error)
    return;
  const struct oak_registered_record_t* sd = null;
  const int idx = oakc_require_record_field(c, recv, fname, 0, &sd);
  (void)sd;
  if (idx < 0)
    return;
  oak_compiler_compile_node(c, recv);
  oak_compiler_emit_op(c,
                       OAK_OP_GET_FIELD,
                       oak_compiler_loc_from_token(fname->token),
                       OAK_ARG_U8((u8)idx));
}
