#pragma once

/*
 * Shared scaffolding for the Oak test suites.
 *
 * Every suite is a plain utest.h translation unit. What this header adds on
 * top is the three things the suites all need and should not each reinvent:
 *
 *   1. A per-test tracking allocator whose teardown fails the test if a single
 *      allocation leaked. Declare it once per file with OAK_TEST_SUITE(name),
 *      then write tests as UTEST_F(name, ...) and allocate through OAK_A.
 *   2. `oak_test_source()` -- drives a source string through the whole
 *      lex/parse/compile/run pipeline and reports what happened at every
 *      stage, including every compiler diagnostic and anything the program or
 *      the VM wrote to stdout and stderr.
 *   3. The OAK_EXPECT_*_CASES macros, which run a table of {source, expected}
 *      rows. utest's EXPECT_* assertions are non-fatal, so one test reports
 *      every failing row rather than stopping at the first.
 */

#include "utest.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_compiler.h"
#include "oak_count_of.h"
#include "oak_lexer.h"
#include "oak_parser.h"
#include "oak_token.h"
#include "oak_types.h"
#include "oak_vm.h"

#include <stdio.h>

/* Room for a cascade, not a single message: one diagnostic's `message` field is
 * itself char[512], and every diagnostic gets joined into this buffer so a test
 * can match against all of them. Truncation is reported rather than silent (see
 * oak_test_source_opts), but the buffer is sized so it does not normally
 * happen. */
#define OAK_TEST_DIAG_MAX   4096
#define OAK_TEST_OUTPUT_MAX 4096

/* Placed in `out`/`err` when the stream could not be redirected at all, so a
 * capture failure is distinguishable from a program that printed nothing. */
#define OAK_TEST_CAPTURE_FAILED "<output capture unavailable>"

/* Outcome of driving one source string through the pipeline. Stages that were
 * never reached leave their fields at the zero value, so a caller can always
 * ask "did it parse?" before trusting `run`. */
typedef struct oak_run_result oak_run_result_t;
struct oak_run_result
{
  int parsed;          /* parser produced a root node */
  int compiled;        /* compiler produced a chunk */
  int error_count;     /* compile diagnostics reported */
  oak_vm_result_t run; /* only meaningful when `compiled` */
  /* Every compile diagnostic, newline-separated; "" when there were none. */
  char diag[OAK_TEST_DIAG_MAX];
  /* Captured across the VM run: everything the program printed, and
   * everything the VM reported as a runtime error. */
  char out[OAK_TEST_OUTPUT_MAX];
  char err[OAK_TEST_OUTPUT_MAX];
};

/* Compile and run `src` with a default set of options bound to `a`.
 *
 * The options always carry an explicit allocator: oak_compile_ex() falls back
 * to the process-wide system allocator when given none, and that fallback is
 * why compiler allocations used to escape the leak check entirely. */
oak_run_result_t oak_test_source(oak_allocator_t* a, const char* src);

/* Same, but with caller-supplied options (native bindings, module registry,
 * source name). `opts->allocator` should be `a`. */
oak_run_result_t oak_test_source_opts(oak_allocator_t* a,
                                      const char* src,
                                      oak_compile_options_t* opts);

/* Lex and parse only -- for suites that assert on AST shape. The caller owns
 * both results and must free the parser result before the lexer result. */
typedef struct oak_parse_fixture oak_parse_fixture_t;
struct oak_parse_fixture
{
  oak_lexer_result_t* lexer;
  oak_parser_result_t* parsed;
  const oak_ast_node_t* root; /* null when the source failed to parse */
};

oak_parse_fixture_t oak_test_parse(oak_allocator_t* a, const char* src);
void oak_test_parse_free(oak_parse_fixture_t* fx);

/* Assertions on AST shape. These name the offending kind rather than just
 * reporting "false", which is the difference between a usable failure and a
 * puzzle when a grammar change moves a node. */
#define OAK_EXPECT_KIND(node, expected)                                        \
  do                                                                           \
  {                                                                            \
    const oak_ast_node_t* oak_n = (node);                                      \
    if (!oak_n)                                                                \
    {                                                                          \
      UTEST_PRINTF("  %s: node is null, want %s\n", #node, #expected);         \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
    else if (oak_n->kind != (expected))                                        \
    {                                                                          \
      UTEST_PRINTF("  %s: kind %s, want %s\n",                                 \
                   #node,                                                      \
                   oak_ast_node_kind_name(oak_n->kind),                        \
                   #expected);                                                 \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

/*
 * Null-safe operand accessors.
 *
 * OAK_EXPECT_KIND is non-fatal by design -- it records the failure and lets the
 * test report every wrong node in one run. That makes a bare `n->lhs` on the
 * following line a crash waiting for the first grammar change that moves a
 * node, which would take down the whole binary and defeat the per-suite
 * isolation. These return null instead, and OAK_EXPECT_KIND reports null as a
 * plain "node is null" miss.
 */
const oak_ast_node_t* oak_test_lhs(const oak_ast_node_t* node);
const oak_ast_node_t* oak_test_rhs(const oak_ast_node_t* node);

/* Reads an INT node's value, reporting a miss rather than dereferencing when
 * the node is absent or not an INT. */
#define OAK_EXPECT_INT(node, expected)                                         \
  do                                                                           \
  {                                                                            \
    const oak_ast_node_t* oak_n = (node);                                      \
    if (!oak_n || oak_n->kind != OAK_NODE_INT)                                 \
    {                                                                          \
      UTEST_PRINTF("  %s: not an int node, want value %d\n",                   \
                   #node,                                                      \
                   (int)(expected));                                           \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
    else                                                                       \
    {                                                                          \
      EXPECT_EQ((int)(expected), oak_token_as_i32(oak_n->token));              \
    }                                                                          \
  } while (0)

#define OAK_EXPECT_CHILDREN(node, expected)                                    \
  do                                                                           \
  {                                                                            \
    const usize oak_count = oak_ast_node_child_count(node);                    \
    if (oak_count != (usize)(expected))                                        \
    {                                                                          \
      UTEST_PRINTF("  %s: %u children, want %u\n",                             \
                   #node,                                                      \
                   (unsigned)oak_count,                                        \
                   (unsigned)(expected));                                      \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

/* 1 when `haystack` contains `needle`; a null/empty needle matches anything. */
int oak_test_contains(const char* haystack, const char* needle);

/*
 * An in-process pipe, for suites that have to hand a FILE* to code that reads
 * or writes a stream. Nothing here touches the filesystem, so there are no
 * scratch files to name, clean up, or leave behind on a killed run.
 *
 * Capacity is the one caveat: nothing drains the pipe until oak_test_pipe_read
 * is called, so the total written between creation and that call has to fit in
 * the pipe buffer (requested at 1 MiB). That is far above anything these tests
 * produce.
 */
typedef struct oak_test_pipe oak_test_pipe_t;
struct oak_test_pipe
{
  FILE* read_end;
  FILE* write_end;
};

/* 1 on success; on failure both ends are null. */
int oak_test_pipe_new(oak_test_pipe_t* p);

/* Closes the write end -- without which the read below could never see EOF --
 * then reads everything buffered into `out` as a C string. Returns the byte
 * count, which is capped at `cap - 1`. */
usize oak_test_pipe_read(oak_test_pipe_t* p, char* out, usize cap);

void oak_test_pipe_free(oak_test_pipe_t* p);

/*
 * Redirection of one whole stream, for the cases oak_test_source() does not
 * cover: code that writes straight to stdout/stderr rather than through the
 * pipeline. oak_log() is the one that matters -- it holds no configurable
 * stream, so the only way to read an allocator's leak report back is to point
 * fd 2 somewhere else for the duration.
 *
 * Same pipe capacity caveat as above: nothing drains until oak_capture_end.
 */
typedef struct oak_capture oak_capture_t;
struct oak_capture
{
  FILE* stream; /* the stream being redirected */
  int read_fd;
  int saved_fd;
};

void oak_capture_begin(oak_capture_t* c, FILE* stream);

/* Restores the stream and reads what was written into `out`, which is set to
 * OAK_TEST_CAPTURE_FAILED if redirection never got set up. */
void oak_capture_end(oak_capture_t* c, char* out, usize cap);

/* 1 when `got` equals `want` ignoring trailing whitespace on both, and
 * treating CRLF as LF so expectations are written the same way on every
 * platform. A null `want` matches anything. */
int oak_test_output_equals(const char* got, const char* want);

/* One expected token. Position fields are always checked; the value fields are
 * checked only for the token kinds they apply to, so a row can name just the
 * kind and where it starts. */
typedef struct oak_expect_token oak_expect_token_t;
struct oak_expect_token
{
  oak_token_kind_t kind;
  int line;
  int column;
  int offset;
  const char* text; /* STRING / IDENT */
  int integer;      /* INT */
  float floating;   /* FLOAT */
};

/* A single-line token on line 1, where column and offset coincide. */
#define OAK_TOKEN_AT(k, pos)                                                   \
  {                                                                            \
    (k), 1, (pos), (pos), null, 0, 0.0f                                        \
  }

/* Compares the lexer's tokens against `expected` element for element, and
 * fails if either side runs out first. Returns 1 on a full match; on mismatch
 * returns 0 and writes a description of the first difference to `msg`. */
int oak_test_tokens_match(const oak_lexer_result_t* lexer,
                          const oak_expect_token_t* expected,
                          usize count,
                          char* msg,
                          usize msg_cap);

#define OAK_EXPECT_TOKENS(lexer, tbl)                                          \
  do                                                                           \
  {                                                                            \
    char oak_msg[512];                                                         \
    if (!oak_test_tokens_match(                                                \
            (lexer), (tbl), oak_count_of(tbl), oak_msg, sizeof(oak_msg)))      \
    {                                                                          \
      UTEST_PRINTF("  %s\n", oak_msg);                                         \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

/* Print the full stage-by-stage outcome. Called by the table macros when a row
 * fails, so failures say which stage went wrong instead of just "false". */
void oak_test_explain(const oak_run_result_t* r, const char* src);

/*
 * Declares the per-test fixture for one suite. Invoke it exactly once near the
 * top of each suite file, then write tests as UTEST_F(<suite>, name).
 *
 * The fixture's struct tag IS the suite name, because utest builds each test's
 * reported name as "<fixture>.<test>". Naming it after the file is what makes
 * `oak_tests --filter='lexer.*'` select exactly that file's tests -- which is
 * how meson registers the suites individually.
 *
 * utest emits the setup/teardown as file-static functions, so this has to be a
 * macro expanded per translation unit rather than one shared definition.
 */
#define OAK_TEST_SUITE(suite)                                                  \
  struct suite                                                                 \
  {                                                                            \
    oak_allocator_t alloc;                                                     \
  };                                                                           \
                                                                               \
  UTEST_F_SETUP(suite)                                                         \
  {                                                                            \
    (void)utest_result;                                                        \
    oak_tracking_allocator_init(&utest_fixture->alloc);                        \
  }                                                                            \
                                                                               \
  UTEST_F_TEARDOWN(suite)                                                      \
  {                                                                            \
    /* Non-zero means allocations outlived the test; the allocator has already \
     * logged each leak's file and line to stderr. */                          \
    const int oak_leaked =                                                     \
        utest_fixture->alloc.shutdown(&utest_fixture->alloc);                  \
    EXPECT_EQ(0, oak_leaked);                                                  \
  }                                                                            \
                                                                               \
  /* Consumes the trailing semicolon at the call site. */                      \
  typedef struct suite suite##_declared_t

/* The tracking allocator for the running test. */
#define OAK_A (&utest_fixture->alloc)

/*
 * Compare an enum constant against an enum-typed value.
 *
 * A bare EXPECT_EQ warns under -Wsign-compare here: C types enum *constants*
 * as int, while GCC gives an enum-typed lvalue an unsigned underlying type
 * when no enumerator is negative. Comparing as int is well-defined for every
 * enum in this codebase and keeps the call sites readable.
 */
#define OAK_EXPECT_ENUM(expected, actual)                                      \
  EXPECT_EQ((int)(expected), (int)(actual))

/* One row of a case table. `want` is a substring the relevant error message
 * must contain; it is unused for the "must succeed" tables. */
typedef struct oak_case oak_case_t;
struct oak_case
{
  const char* src;
  const char* want;
};

/*
 * Rejects `want == ""` in the tables that match a message substring.
 *
 * Every string contains the empty string, so an empty expectation quietly
 * degrades the row to "some error happened" -- the exact weakness these tables
 * exist to remove, and invisible at the call site next to rows that do assert.
 * `null` is the deliberate way to say "any message will do".
 */
#define OAK_REQUIRE_WANT(tbl, i)                                               \
  do                                                                           \
  {                                                                            \
    if ((tbl)[i].want && !(tbl)[i].want[0])                                    \
    {                                                                          \
      UTEST_PRINTF("  row %u: empty expectation matches every message; "       \
                   "give a substring, or null to skip the check\n",            \
                   (unsigned)(i));                                             \
      *utest_result = UTEST_TEST_FAILURE;                                      \
    }                                                                          \
  } while (0)

/* Every row must lex, parse, compile, and run to completion. */
#define OAK_EXPECT_OK_CASES(tbl)                                               \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < oak_count_of(tbl); ++oak_i)                  \
    {                                                                          \
      const oak_run_result_t oak_r =                                           \
          oak_test_source(OAK_A, (tbl)[oak_i].src);                            \
      if (!oak_r.parsed || !oak_r.compiled || oak_r.run != OAK_VM_OK)          \
      {                                                                        \
        UTEST_PRINTF("  expected success, row %u:\n", (unsigned)oak_i);        \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/* Every row must reach the compiler and be rejected, with a diagnostic
 * containing `want`. Asserting the message is what stops these tests from
 * passing when the compiler rejects the program for an unrelated reason. */
#define OAK_EXPECT_COMPILE_ERROR_CASES(tbl)                                    \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < oak_count_of(tbl); ++oak_i)                  \
    {                                                                          \
      const oak_run_result_t oak_r =                                           \
          oak_test_source(OAK_A, (tbl)[oak_i].src);                            \
      OAK_REQUIRE_WANT(tbl, oak_i);                                            \
      if (oak_r.compiled)                                                      \
      {                                                                        \
        UTEST_PRINTF("  expected a compile error, row %u:\n", (unsigned)oak_i); \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
      else if (!oak_test_contains(oak_r.diag, (tbl)[oak_i].want))              \
      {                                                                        \
        UTEST_PRINTF("  wrong diagnostic, row %u: want substring '%s'\n",      \
                     (unsigned)oak_i,                                          \
                     (tbl)[oak_i].want ? (tbl)[oak_i].want : "");              \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/* Every row must be rejected before the VM runs, either by the parser or by
 * the compiler. Use for malformed syntax that either stage may legitimately
 * catch; prefer OAK_EXPECT_COMPILE_ERROR_CASES when the stage is settled. */
#define OAK_EXPECT_REJECTED_CASES(tbl)                                         \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < oak_count_of(tbl); ++oak_i)                  \
    {                                                                          \
      const oak_run_result_t oak_r =                                           \
          oak_test_source(OAK_A, (tbl)[oak_i].src);                            \
      if (oak_r.compiled)                                                      \
      {                                                                        \
        UTEST_PRINTF("  expected rejection, row %u:\n", (unsigned)oak_i);      \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/* Every row must compile, then fail at runtime with stderr containing `want`. */
#define OAK_EXPECT_RUNTIME_ERROR_CASES(tbl)                                    \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < oak_count_of(tbl); ++oak_i)                  \
    {                                                                          \
      const oak_run_result_t oak_r =                                           \
          oak_test_source(OAK_A, (tbl)[oak_i].src);                            \
      OAK_REQUIRE_WANT(tbl, oak_i);                                            \
      if (!oak_r.compiled || oak_r.run == OAK_VM_OK)                           \
      {                                                                        \
        UTEST_PRINTF("  expected a runtime error, row %u:\n", (unsigned)oak_i); \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
      else if (!oak_test_contains(oak_r.err, (tbl)[oak_i].want))               \
      {                                                                        \
        UTEST_PRINTF("  wrong runtime error, row %u: want substring '%s'\n",   \
                     (unsigned)oak_i,                                          \
                     (tbl)[oak_i].want ? (tbl)[oak_i].want : "");              \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

/*
 * Every row must run to completion and print exactly `want`.
 *
 * This is the readable way to assert on a computed value: the program prints
 * it and the test compares strings. The alternative the old suite used --
 * `if wrong { print([1][3]); }`, turning a bad result into an out-of-bounds
 * error -- proves only that something was wrong, never what the value was.
 *
 * Trailing whitespace and newlines are ignored so rows can print one value per
 * line without the expectation carrying line-ending noise.
 */
#define OAK_EXPECT_OUTPUT_CASES(tbl)                                           \
  do                                                                           \
  {                                                                            \
    for (usize oak_i = 0; oak_i < oak_count_of(tbl); ++oak_i)                  \
    {                                                                          \
      const oak_run_result_t oak_r =                                           \
          oak_test_source(OAK_A, (tbl)[oak_i].src);                            \
      if (!oak_r.compiled || oak_r.run != OAK_VM_OK)                           \
      {                                                                        \
        UTEST_PRINTF("  expected a clean run, row %u:\n", (unsigned)oak_i);    \
        oak_test_explain(&oak_r, (tbl)[oak_i].src);                            \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
      else if (!oak_test_output_equals(oak_r.out, (tbl)[oak_i].want))          \
      {                                                                        \
        UTEST_PRINTF("  wrong output, row %u:\n    want: %s\n    got:  %s\n",  \
                     (unsigned)oak_i,                                          \
                     (tbl)[oak_i].want ? (tbl)[oak_i].want : "",               \
                     oak_r.out);                                               \
        UTEST_PRINTF("    source: %s\n", (tbl)[oak_i].src);                    \
        *utest_result = UTEST_TEST_FAILURE;                                    \
      }                                                                        \
    }                                                                          \
  } while (0)
