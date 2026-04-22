#include "oak_parser_internal.h"

int oak_node_is_unary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_UNARY;
}

int oak_node_is_binary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_BINARY;
}
