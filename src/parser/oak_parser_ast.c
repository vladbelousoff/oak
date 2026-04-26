#include "oak_parser_internal.h"

#include "oak_list.h"

int oak_node_is_unary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_UNARY;
}

int oak_node_is_binary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_BINARY;
}

int oak_node_is_token_terminal(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_TOKEN;
}

usize oak_ast_node_child_count(const struct oak_ast_node_t* node)
{
  if (!node)
    return 0;

  if (oak_node_is_unary_op(node->kind))
    return node->child ? 1 : 0;

  if (oak_node_is_binary_op(node->kind))
    return (node->lhs ? 1 : 0) + (node->rhs ? 1 : 0);

  if (oak_node_is_token_terminal(node->kind))
    return 0;

  return oak_list_length(&node->children);
}

struct oak_ast_node_t*
oak_ast_node_child_at(const struct oak_ast_node_t* node, const usize index)
{
  if (!node)
    return null;

  if (oak_node_is_unary_op(node->kind))
    return (index == 0 && node->child) ? node->child : null;

  if (oak_node_is_binary_op(node->kind))
  {
    usize i = 0;
    if (node->lhs)
    {
      if (i == index)
        return node->lhs;
      ++i;
    }
    if (node->rhs)
    {
      if (i == index)
        return node->rhs;
    }
    return null;
  }

  if (oak_node_is_token_terminal(node->kind))
    return null;

  usize i;
  struct oak_list_entry_t* pos;
  oak_list_for_each_indexed(i, pos, &node->children)
  {
    if (i == index)
      return oak_container_of(pos, struct oak_ast_node_t, link);
  }
  return null;
}

const char* oak_ast_node_kind_name(const enum oak_node_kind_t kind)
{
  switch (kind)
  {
  case OAK_NODE_NONE:
    return "NONE";
  case OAK_NODE_PROGRAM:
    return "PROGRAM";
  case OAK_NODE_PROGRAM_ITEM:
    return "PROGRAM_ITEM";
  case OAK_NODE_RECORD_DECL:
    return "RECORD_DECL";
  case OAK_NODE_TYPE_NAME:
    return "TYPE_NAME";
  case OAK_NODE_RECORD_FIELD_DECL:
    return "RECORD_FIELD_DECL";
  case OAK_NODE_RECORD_MEMBER:
    return "RECORD_MEMBER";
  case OAK_NODE_RECORD_FIELDS:
    return "RECORD_FIELDS";
  case OAK_NODE_ENUM_DECL:
    return "ENUM_DECL";
  case OAK_NODE_IDENT:
    return "IDENT";
  case OAK_NODE_STMT:
    return "STMT";
  case OAK_NODE_STMT_EXPR:
    return "STMT_EXPR";
  case OAK_NODE_EXPR:
    return "EXPR";
  case OAK_NODE_INT:
    return "INT";
  case OAK_NODE_FLOAT:
    return "FLOAT";
  case OAK_NODE_STRING:
    return "STRING";
  case OAK_NODE_EXPR_PRIMARY:
    return "EXPR_PRIMARY";
  case OAK_NODE_STMT_ASSIGNMENT:
    return "STMT_ASSIGNMENT";
  case OAK_NODE_STMT_LET_ASSIGNMENT:
    return "STMT_LET_ASSIGNMENT";
  case OAK_NODE_BINARY_ADD:
    return "BINARY_ADD";
  case OAK_NODE_BINARY_SUB:
    return "BINARY_SUB";
  case OAK_NODE_BINARY_MUL:
    return "BINARY_MUL";
  case OAK_NODE_BINARY_DIV:
    return "BINARY_DIV";
  case OAK_NODE_BINARY_MOD:
    return "BINARY_MOD";
  case OAK_NODE_BINARY_EQ:
    return "BINARY_EQ";
  case OAK_NODE_BINARY_NEQ:
    return "BINARY_NEQ";
  case OAK_NODE_BINARY_LESS:
    return "BINARY_LESS";
  case OAK_NODE_BINARY_LESS_EQ:
    return "BINARY_LESS_EQ";
  case OAK_NODE_BINARY_GREATER:
    return "BINARY_GREATER";
  case OAK_NODE_BINARY_GREATER_EQ:
    return "BINARY_GREATER_EQ";
  case OAK_NODE_BINARY_AND:
    return "BINARY_AND";
  case OAK_NODE_BINARY_OR:
    return "BINARY_OR";
  case OAK_NODE_UNARY_NEG:
    return "UNARY_NEG";
  case OAK_NODE_UNARY_NOT:
    return "UNARY_NOT";
  case OAK_NODE_FN_DECL:
    return "FN_DECL";
  case OAK_NODE_FN_PROTO:
    return "FN_PROTO";
  case OAK_NODE_FN_HEAD:
    return "FN_HEAD";
  case OAK_NODE_FN_PREFIX:
    return "FN_PREFIX";
  case OAK_NODE_FN_PARAMS_AND_RET:
    return "FN_PARAMS_AND_RET";
  case OAK_NODE_FN_PARAM_LIST:
    return "FN_PARAM_LIST";
  case OAK_NODE_FN_PARAMS:
    return "FN_PARAMS";
  case OAK_NODE_FN_RETURN_TYPE:
    return "FN_RETURN_TYPE";
  case OAK_NODE_FN_RECEIVER:
    return "FN_RECEIVER";
  case OAK_NODE_FN_PARAM:
    return "FN_PARAM";
  case OAK_NODE_FN_PARAM_SELF:
    return "FN_PARAM_SELF";
  case OAK_NODE_SELF:
    return "SELF";
  case OAK_NODE_MUT_KEYWORD:
    return "MUT_KEYWORD";
  case OAK_NODE_FN_CALL:
    return "FN_CALL";
  case OAK_NODE_FN_CALL_ARG:
    return "FN_CALL_ARG";
  case OAK_NODE_STMT_RETURN:
    return "STMT_RETURN";
  case OAK_NODE_STMT_IF:
    return "STMT_IF";
  case OAK_NODE_ELSE_BLOCK:
    return "ELSE_BLOCK";
  case OAK_NODE_STMT_WHILE:
    return "STMT_WHILE";
  case OAK_NODE_STMT_FOR_FROM:
    return "STMT_FOR_FROM";
  case OAK_NODE_STMT_FOR_IN:
    return "STMT_FOR_IN";
  case OAK_NODE_STMT_BREAK:
    return "STMT_BREAK";
  case OAK_NODE_STMT_CONTINUE:
    return "STMT_CONTINUE";
  case OAK_NODE_STMT_ADD_ASSIGN:
    return "STMT_ADD_ASSIGN";
  case OAK_NODE_STMT_SUB_ASSIGN:
    return "STMT_SUB_ASSIGN";
  case OAK_NODE_STMT_MUL_ASSIGN:
    return "STMT_MUL_ASSIGN";
  case OAK_NODE_STMT_DIV_ASSIGN:
    return "STMT_DIV_ASSIGN";
  case OAK_NODE_STMT_MOD_ASSIGN:
    return "STMT_MOD_ASSIGN";
  case OAK_NODE_MEMBER_ACCESS:
    return "MEMBER_ACCESS";
  case OAK_NODE_TYPE_ARRAY:
    return "TYPE_ARRAY";
  case OAK_NODE_TYPE_MAP:
    return "TYPE_MAP";
  case OAK_NODE_EXPR_EMPTY_ARRAY:
    return "EXPR_EMPTY_ARRAY";
  case OAK_NODE_EXPR_EMPTY_MAP:
    return "EXPR_EMPTY_MAP";
  case OAK_NODE_EXPR_ARRAY_LITERAL:
    return "EXPR_ARRAY_LITERAL";
  case OAK_NODE_ARRAY_LITERAL_ELEMENT:
    return "ARRAY_LITERAL_ELEMENT";
  case OAK_NODE_EXPR_MAP_LITERAL:
    return "EXPR_MAP_LITERAL";
  case OAK_NODE_MAP_LITERAL_ENTRIES:
    return "MAP_LITERAL_ENTRIES";
  case OAK_NODE_MAP_LITERAL_ENTRY:
    return "MAP_LITERAL_ENTRY";
  case OAK_NODE_INDEX_ACCESS:
    return "INDEX_ACCESS";
  case OAK_NODE_EXPR_CAST:
    return "EXPR_CAST";
  case OAK_NODE_EXPR_RECORD_LITERAL:
    return "EXPR_RECORD_LITERAL";
  case OAK_NODE_RECORD_LITERAL_FIELDS:
    return "RECORD_LITERAL_FIELDS";
  case OAK_NODE_RECORD_LITERAL_FIELD:
    return "RECORD_LITERAL_FIELD";
  case OAK_NODE_ENUM_VARIANTS:
    return "ENUM_VARIANTS";
  case OAK_NODE_BLOCK:
    return "BLOCK";
  }
  return "UNKNOWN";
}
