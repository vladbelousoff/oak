#pragma once

#include "oak_chunk.h"
#include "oak_compiler.h"
#include "oak_diagnostic.h"
#include "oak_export.h"
#include "oak_lexer.h"
#include "oak_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Source text to bytecode in one call.
 *
 * The lex/parse/compile pipeline can be driven by hand -- see oak_lexer.h,
 * oak_parser.h and oak_compile_ex in oak_bind.h -- but doing so means holding
 * three separate results alive together and releasing them in an order that
 * matters: the AST arena borrows the lexer's tokens, so the parser result must
 * go first. This type owns all three and gets that right.
 *
 *   oak_compile_options_t opts;
 *   oak_compile_options_init(&opts, &allocator);
 *
 *   oak_program_t prog;
 *   if (oak_program_compile(&prog, source, &opts))
 *   {
 *     oak_vm_t vm;
 *     oak_vm_init(&vm, &allocator);
 *     if (oak_vm_run(&vm, oak_program_chunk(&prog)) != OAK_VM_OK)
 *       report(oak_vm_last_error(&vm));
 *     oak_vm_free(&vm);
 *   }
 *   else
 *   {
 *     oak_diagnostics_print(oak_program_errors(&prog),
 *                           oak_program_error_count(&prog));
 *   }
 *   oak_program_free(&prog);
 *   oak_compile_options_free(&opts);
 *
 * One ordering rule survives, and it is not this type's to enforce: the
 * options must outlive the VM and every native value made from it. Free them
 * last. See the lifetime note on oak_compile_options_t.
 */
/* Defined in oak_bind.h, which this header deliberately does not pull in. */
typedef struct oak_compile_options oak_compile_options_t;

typedef struct oak_program oak_program_t;
struct oak_program
{
  oak_lexer_result_t* lexer;
  oak_parser_result_t* parser;
  oak_compile_result_t compiled;
  oak_allocator_t* allocator;
};

/* Lex, parse and compile `source`. Returns 1 when a chunk was produced, 0 when
 * any stage failed -- read the diagnostics with oak_program_errors.
 *
 * `opts` must be non-null and initialized with oak_compile_options_init; the
 * allocator for every stage comes from opts->allocator, so there is only one
 * place it can be set. Register native bindings on `opts` before calling.
 *
 * `prog` need not be initialized. Release it with oak_program_free in either
 * case, including on failure. */
OAK_API int oak_program_compile(oak_program_t* prog,
                                const char* source,
                                const oak_compile_options_t* opts);

/* The compiled bytecode, or null if compilation failed. Owned by `prog`;
 * borrow it for oak_vm_run. */
OAK_API oak_chunk_t* oak_program_chunk(const oak_program_t* prog);

/* Diagnostics from whichever stage failed -- parsing and compilation cannot
 * both report, since compilation only runs on a clean parse. Null when there
 * are none. Owned by `prog`. */
OAK_API const oak_diagnostic_t* oak_program_errors(const oak_program_t* prog);
OAK_API int oak_program_error_count(const oak_program_t* prog);

/* Release the chunk, AST and tokens in the correct order, and null what it
 * releases, so calling it twice is a no-op. Safe on a zero-initialized
 * program. Does not touch the options. */
OAK_API void oak_program_free(oak_program_t* prog);

#ifdef __cplusplus
}
#endif
