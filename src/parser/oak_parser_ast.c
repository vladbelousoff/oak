#include "oak_parser_internal.h"

#include "oak_log.h"

#include <stdarg.h>

int oak_node_is_unary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_UNARY;
}

int oak_node_is_binary_op(const enum oak_node_kind_t kind)
{
  return oak_grammar[kind].op == OAK_GRAMMAR_BINARY;
}

void oak_ast_node_unpack(const struct oak_ast_node_t* node, ...)
{
  oak_assert(node);
  oak_assert(oak_grammar[node->kind].op != OAK_GRAMMAR_BINARY);
  oak_assert(oak_grammar[node->kind].op != OAK_GRAMMAR_UNARY);

  va_list args;
  va_start(args, node);

  struct oak_list_entry_t* entry;
  oak_list_for_each(entry, &node->children)
  {
    struct oak_ast_node_t* child =
        oak_container_of(entry, struct oak_ast_node_t, link);
    *va_arg(args, struct oak_ast_node_t**) = child;
  }

  va_end(args);
}
