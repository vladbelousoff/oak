#pragma once

#include "oak_list.h"
#include "oak_parser.h"

static oak_ast_node_t* oak_test_ast_child(const oak_ast_node_t* node,
                                          const size_t index)
{
  if (oak_node_grammar_op_unary(node->kind))
    return index == 0 ? node->child : NULL;

  size_t i;
  oak_list_entry_t* pos;
  oak_list_for_each_indexed(i, pos, &node->children)
  {
    if (i == index)
      return oak_container_of(pos, oak_ast_node_t, link);
  }
  return NULL;
}

static size_t oak_test_ast_child_count(const oak_ast_node_t* node)
{
  if (oak_node_grammar_op_unary(node->kind))
    return node->child ? 1 : 0;

  return oak_list_length(&node->children);
}

static oak_result_t oak_test_ast_kind(const oak_ast_node_t* node,
                                      const oak_node_kind_t expected)
{
  if (!node)
    return OAK_FAILURE;
  return node->kind == expected ? OAK_SUCCESS : OAK_FAILURE;
}
