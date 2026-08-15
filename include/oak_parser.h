#pragma once

#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_lexer.h"
#include "oak_token.h"
#include "oak_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oak_node_kind oak_node_kind_t;
enum oak_node_kind
{
  OAK_NODE_NONE,
  OAK_NODE_PROGRAM,
  OAK_NODE_PROGRAM_ITEM,
  OAK_NODE_RECORD_DECL,
  /* record Name;  — bodyless record (no fields) */
  OAK_NODE_RECORD_DECL_EMPTY,
  /* What follows `record`. HEADER is a transparent choice, so a declaration's
   * lhs (child, when bodyless) is either a bare TYPE_NAME or a
   * HEADER_IMPL: binary lhs = TYPE_NAME, rhs = RECORD_IMPLEMENTATIONS, a list
   * of IDENT interface names. */
  OAK_NODE_RECORD_DECL_HEADER,
  OAK_NODE_RECORD_DECL_HEADER_IMPL,
  OAK_NODE_RECORD_IMPLEMENTATIONS,
  OAK_NODE_TYPE_NAME,
  OAK_NODE_RECORD_FIELD_DECL,
  /* One entry in a record body: a field, or a method (optionally wrapped in
   * `export` / `@Attr`). Transparent choice — the member node itself never
   * reaches the compiler, only the field or fn it matched. */
  OAK_NODE_RECORD_MEMBER,
  OAK_NODE_RECORD_MEMBERS,
  OAK_NODE_ENUM_DECL,
  OAK_NODE_IDENT,
  OAK_NODE_STMT,
  OAK_NODE_STMT_EXPR,
  OAK_NODE_EXPR,
  OAK_NODE_INT,
  OAK_NODE_FLOAT,
  OAK_NODE_STRING,
  OAK_NODE_TRUE,
  OAK_NODE_FALSE,
  OAK_NODE_NONE_LITERAL,
  OAK_NODE_EXPR_PRIMARY,
  OAK_NODE_STMT_ASSIGNMENT,
  OAK_NODE_STMT_LET_ASSIGNMENT,
  OAK_NODE_BINARY_ADD,
  OAK_NODE_BINARY_SUB,
  OAK_NODE_BINARY_MUL,
  OAK_NODE_BINARY_DIV,
  OAK_NODE_BINARY_INT_DIV,
  OAK_NODE_BINARY_MOD,
  OAK_NODE_BINARY_EQ,
  OAK_NODE_BINARY_NEQ,
  OAK_NODE_BINARY_LESS,
  OAK_NODE_BINARY_LESS_EQ,
  OAK_NODE_BINARY_GREATER,
  OAK_NODE_BINARY_GREATER_EQ,
  OAK_NODE_BINARY_AND,
  OAK_NODE_BINARY_OR,
  OAK_NODE_UNARY_NEG,
  OAK_NODE_UNARY_NOT,
  OAK_NODE_FN_DECL,
  OAK_NODE_FN_PROTO,
  OAK_NODE_FN_HEAD,
  OAK_NODE_FN_PREFIX,
  OAK_NODE_FN_DECL_BODY,
  OAK_NODE_FN_DECL_SEMICOLON,
  OAK_NODE_FN_PARAMS_AND_RET,
  OAK_NODE_FN_PARAM_LIST,
  OAK_NODE_FN_PARAMS,
  OAK_NODE_FN_RETURN_TYPE,
  OAK_NODE_FN_PARAM,
  OAK_NODE_SELF,
  OAK_NODE_MUT_KEYWORD,
  OAK_NODE_STATIC_KEYWORD,
  /* `mut` or `static` directly after `fn`, hanging off FN_PREFIX. Absent means
   * an instance method with an immutable receiver. Transparent choice, so the
   * node reaching the compiler is MUT_KEYWORD or STATIC_KEYWORD. */
  OAK_NODE_FN_RECEIVER_MODE,
  OAK_NODE_FN_CALL,
  OAK_NODE_FN_CALL_ARG,
  OAK_NODE_STMT_RETURN,
  OAK_NODE_STMT_IF,
  OAK_NODE_ELSE_BLOCK,
  OAK_NODE_STMT_WHILE,
  OAK_NODE_STMT_FOR_FROM,
  OAK_NODE_STMT_FOR_IN,
  OAK_NODE_STMT_BREAK,
  OAK_NODE_STMT_CONTINUE,
  OAK_NODE_STMT_ADD_ASSIGN,
  OAK_NODE_STMT_SUB_ASSIGN,
  OAK_NODE_STMT_MUL_ASSIGN,
  OAK_NODE_STMT_DIV_ASSIGN,
  OAK_NODE_STMT_MOD_ASSIGN,
  OAK_NODE_MEMBER_ACCESS,
  OAK_NODE_TYPE_WEAK,
  OAK_NODE_TYPE_WEAK_BASE,
  OAK_NODE_TYPE_ARRAY,
  OAK_NODE_TYPE_MAP,
  OAK_NODE_EXPR_EMPTY_ARRAY,
  OAK_NODE_EXPR_EMPTY_MAP,
  OAK_NODE_EXPR_ARRAY_LITERAL,
  OAK_NODE_ARRAY_LITERAL_ELEMENT,
  OAK_NODE_EXPR_MAP_LITERAL,
  OAK_NODE_MAP_LITERAL_ENTRIES,
  OAK_NODE_MAP_LITERAL_ENTRY,
  OAK_NODE_INDEX_ACCESS,
  OAK_NODE_EXPR_CAST,
  OAK_NODE_EXPR_RECORD_LITERAL,
  OAK_NODE_RECORD_LITERAL_FIELDS,
  OAK_NODE_RECORD_LITERAL_FIELD,
  OAK_NODE_ENUM_VARIANTS,
  OAK_NODE_BLOCK,
  OAK_NODE_IMPORT_DECL,
  OAK_NODE_IMPORT_PATH,
  /* interface IName { fn_decl* }
   * Binary: lhs = IDENT (interface name), rhs = INTERFACE_MEMBERS */
  OAK_NODE_INTERFACE_DECL,
  OAK_NODE_INTERFACE_MEMBERS,
  /* @AttributeName syntax for records, enums, and functions.
   * ATTR: unary, child = IDENT (the attribute name after '@').
   * ATTR_DECL: sequence, children = ATTR... followed by the declaration node.
   * ATTR_DECL_BODY: choice node (transparent — resolved to the actual decl). */
  OAK_NODE_ATTR,
  OAK_NODE_ATTR_DECL,
  OAK_NODE_ATTR_DECL_BODY,
  OAK_NODE_EXPORT_DECL,
  OAK_NODE_EXPORT_DECL_BODY,
  OAK_NODE_IMPORT_SELECTIVE,
  OAK_NODE_IMPORT_WILDCARD,
  OAK_NODE_IMPORT_NAMES,
  OAK_NODE_IMPORT_NAME,
  OAK_NODE_IMPORT_ALIAS,
  /* Element type inside an array annotation.
   * Transparent choice — returns the matched child directly. */
  OAK_NODE_TYPE_ARRAY_BASE,
  /* Restricted choice for function names.
   * Transparent (CHOICE): returns the matched child directly. */
  OAK_NODE_FN_NAME,
  /* fn(x: number) -> number { return x; }
   * Anonymous function expression.
   * Binary: lhs = FN_PARAMS_AND_RET, rhs = BLOCK. */
  OAK_NODE_EXPR_FN,
  /* fn(number, string) -> bool — function type annotation.
   * Binary: lhs = TYPE_FN_PARAMS, rhs = FN_RETURN_TYPE?. */
  OAK_NODE_TYPE_FN,
  /* (number, string) — comma-separated parameter types inside a fn type. */
  OAK_NODE_TYPE_FN_PARAMS,
  /* new T[] — typed empty array constructor.
   * Unary: child = TYPE_ARRAY. */
  OAK_NODE_EXPR_NEW_ARRAY,
  /* new [K:V] — typed empty map constructor.
   * Unary: child = TYPE_MAP. */
  OAK_NODE_EXPR_NEW_MAP,
};

typedef struct oak_ast_node oak_ast_node_t;
struct oak_ast_node
{
  oak_list_entry_t link;
  oak_node_kind_t kind;

  union
  {
    const oak_token_t* token;
    oak_list_entry_t children;
    oak_ast_node_t* child;

    struct
    {
      oak_ast_node_t* lhs;
      oak_ast_node_t* rhs;
    };
  };
};

/* The AST and its diagnostics.
 *
 * Opaque, and heap-allocated by oak_parse, matching oak_lexer_result_t: the
 * nodes live in an arena the result owns, so there is nothing an embedder can
 * usefully do with the struct itself. Read it through the accessors below and
 * release it with oak_parser_free. */
typedef struct oak_parser_result oak_parser_result_t;

/* Parse the token stream in `lexer` as `kind` (OAK_NODE_PROGRAM for a whole
 * program). Returns null only if the result itself could not be allocated; a
 * parse failure still returns a result, with the diagnostics readable through
 * oak_parser_errors and a null root.
 *
 * The AST borrows the lexer's tokens, so `lexer` must outlive the result --
 * free the parser result first, then the lexer. */
OAK_API oak_parser_result_t* oak_parse(const oak_lexer_result_t* lexer,
                                       oak_node_kind_t kind,
                                       oak_allocator_t* allocator);

/* The root node, or null when parsing failed. Owned by `result`. */
OAK_API oak_ast_node_t* oak_parser_root(const oak_parser_result_t* result);

OAK_API int oak_parser_error_count(const oak_parser_result_t* result);
OAK_API const oak_diagnostic_t*
oak_parser_errors(const oak_parser_result_t* result);

/* Releases the arena holding every node, and the result itself. Safe on
 * null. */
OAK_API void oak_parser_free(oak_parser_result_t* result);

OAK_API int oak_node_is_unary_op(oak_node_kind_t kind);
OAK_API int oak_node_is_binary_op(oak_node_kind_t kind);
OAK_API int oak_node_is_token_terminal(oak_node_kind_t kind);

OAK_API usize oak_ast_node_child_count(const oak_ast_node_t* node);
OAK_API oak_ast_node_t*
oak_ast_node_child_at(const oak_ast_node_t* node, usize index);

OAK_API const char* oak_ast_node_kind_name(oak_node_kind_t kind);

#ifdef __cplusplus
}
#endif
