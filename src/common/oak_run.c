#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_lexer.h"
#include "oak_log.h"
#include "oak_mem.h"
#include "oak_parser.h"
#include "oak_stdlib.h"
#include "oak_vm.h"

#include <string.h>

int oak_run(const char* code)
{
    oak_mem_init();

    struct oak_parser_result_t result = { 0 };
    struct oak_compile_result_t cr = { 0 };
    struct oak_compile_options_t compile_opts;
    oak_compile_options_init(&compile_opts);
    compile_opts.source_name = "";
    compile_opts.emit_debug_info = 1;
    oak_stdlib_register(&compile_opts);
    int exit_code = 1;

    int code_size = strlen(code);
    struct oak_lexer_result_t* lexer = oak_lexer_tokenize(
			code, code_size);
    oak_parse(lexer, OAK_NODE_PROGRAM, &result);

    for (int i = 0; i < oak_parser_error_count(&result); i++)
    {
			const struct oak_diagnostic_t* d = &oak_parser_errors(&result)[i];
			if (d->line > 0)
			oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
			else
			oak_log(OAK_LOG_ERROR, "%s", d->message);
    }

    const struct oak_ast_node_t* const root = oak_parser_root(&result);
    if (root)
    {
			oak_compile_ex(root, &compile_opts, &cr);

			for (int i = 0; i < cr.error_count; i++)
			{
			const struct oak_diagnostic_t* d = &cr.errors[i];
			if (d->line > 0)
				oak_log(OAK_LOG_ERROR, "%d:%d: %s", d->line, d->column, d->message);
			else
				oak_log(OAK_LOG_ERROR, "%s", d->message);
			}

			if (cr.chunk)
			{
				struct oak_vm_t vm;
				oak_vm_init(&vm);
				exit_code = oak_vm_run(&vm, cr.chunk) != OAK_VM_OK;
				oak_vm_free(&vm);
			}
    }

    oak_compile_result_free(&cr);
    oak_compile_options_free(&compile_opts);
    oak_parser_free(&result);
    oak_lexer_free(lexer);
    oak_mem_shutdown();

    return exit_code;
}
