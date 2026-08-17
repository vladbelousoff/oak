#include "oak_program.h"

#include "oak_bind.h"

#include <stdio.h>
#include <string.h>

int oak_program_compile(oak_program_t* prog,
                        const char* source,
                        const oak_compile_options_t* opts)
{
  if (!prog)
    return 0;
  memset(prog, 0, sizeof *prog);
  if (!source || !opts || !opts->allocator)
    return 0;

  prog->allocator = opts->allocator;
  prog->lexer = oak_lexer_tokenize(source, prog->allocator);
  if (!prog->lexer)
    return 0;

  /* Bytes the lexer could not turn into tokens -- invalid UTF-8, a bad escape,
   * an unterminated literal -- have to fail the compile here, or parsing would
   * run on a stream with the offending text silently missing. The lexer logs
   * the specifics; report one diagnostic so an embedder checking
   * oak_program_errors() is never told the compile failed for no reason. */
  const int lex_errors = oak_lexer_error_count(prog->lexer);
  if (lex_errors > 0)
  {
    prog->compiled.error_count = 1;
    prog->compiled.errors[0].line = 0;
    prog->compiled.errors[0].column = 0;
    snprintf(prog->compiled.errors[0].message,
             sizeof(prog->compiled.errors[0].message),
             "%d lexical error%s, see the log for details",
             lex_errors,
             lex_errors == 1 ? "" : "s");
    return 0;
  }

  prog->parser =
      oak_parse(prog->lexer, OAK_NODE_PROGRAM, prog->allocator);
  const oak_ast_node_t* const root = oak_parser_root(prog->parser);
  if (!root || oak_parser_error_count(prog->parser) > 0)
    return 0;

  oak_compile_ex(root, opts, &prog->compiled);
  return prog->compiled.chunk != OAK_NULL;
}

oak_chunk_t* oak_program_chunk(const oak_program_t* prog)
{
  return prog ? prog->compiled.chunk : OAK_NULL;
}

const oak_diagnostic_t* oak_program_errors(const oak_program_t* prog)
{
  if (!prog)
    return OAK_NULL;
  /* Compilation only runs on a clean parse, so at most one stage has errors. */
  if (oak_parser_error_count(prog->parser) > 0)
    return oak_parser_errors(prog->parser);
  if (prog->compiled.error_count > 0)
    return prog->compiled.errors;
  return OAK_NULL;
}

int oak_program_error_count(const oak_program_t* prog)
{
  if (!prog)
    return 0;
  const int parse_errors = oak_parser_error_count(prog->parser);
  return parse_errors > 0 ? parse_errors : prog->compiled.error_count;
}

void oak_program_free(oak_program_t* prog)
{
  if (!prog)
    return;
  /* Order matters: the chunk first, then the AST arena, then the tokens the
   * arena borrows from. */
  oak_compile_result_free(&prog->compiled);
  oak_parser_free(prog->parser);
  prog->parser = OAK_NULL;
  if (prog->lexer)
  {
    oak_lexer_free(prog->lexer);
    prog->lexer = OAK_NULL;
  }
  prog->allocator = OAK_NULL;
}
