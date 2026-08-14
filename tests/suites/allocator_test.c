#include "oak_test_support.h"

#include <stdlib.h>
#include <string.h>

/*
 * The allocator takes an oak_source_loc_t rather than baking __FILE__/__LINE__
 * in behind a macro because of attribution: a helper that allocates on someone
 * else's behalf has to be able to report its caller. These tests pin that down
 * from both ends -- that a location handed to oak_alloc arrives at the
 * allocator unchanged, and that it is what the leak report prints.
 *
 * The suite deliberately leaks in places, so it cannot use OAK_TEST_SUITE:
 * that fixture's teardown asserts the allocator finished clean. Each test
 * drives its own allocator and calls shutdown() itself.
 */

/* A stand-in for a script path. Static storage because the allocator borrows
 * `file` and only reads it back at shutdown -- the lifetime rule the header
 * documents. */
static const char SCRIPT[] = "script.oak";

/*
 * Records what the vtable was handed, so a test can assert on the location
 * without going through the tracking allocator's printed output.
 */
typedef struct record_state record_state_t;
struct record_state
{
  oak_source_loc_t last_alloc;
  oak_source_loc_t last_realloc;
  oak_source_loc_t last_free;
  int alloc_calls;
};

static void* record_alloc(oak_allocator_t* self,
                          usize size,
                          oak_source_loc_t at)
{
  record_state_t* state = self->state;
  state->last_alloc = at;
  ++state->alloc_calls;
  return malloc(size);
}

static void* record_realloc(oak_allocator_t* self,
                            void* ptr,
                            usize size,
                            oak_source_loc_t at)
{
  record_state_t* state = self->state;
  state->last_realloc = at;
  return realloc(ptr, size);
}

static void record_free(oak_allocator_t* self, void* ptr, oak_source_loc_t at)
{
  record_state_t* state = self->state;
  state->last_free = at;
  free(ptr);
}

static int record_shutdown(oak_allocator_t* self)
{
  (void)self;
  return 0;
}

static oak_allocator_t record_allocator(record_state_t* state)
{
  oak_allocator_t a;
  a.alloc = record_alloc;
  a.realloc = record_realloc;
  a.free = record_free;
  a.shutdown = record_shutdown;
  a.state = state;
  a.malloc_fn = null;
  a.realloc_fn = null;
  a.free_fn = null;
  return a;
}

static void record_state_init(record_state_t* state)
{
  memset(state, 0, sizeof *state);
}

UTEST(allocator, explicit_location_reaches_the_allocator)
{
  record_state_t state;
  oak_allocator_t a;
  void* p;

  record_state_init(&state);
  a = record_allocator(&state);

  p = oak_alloc(&a, 16, oak_source_loc_make(SCRIPT, 42));
  ASSERT_TRUE(p != null);
  EXPECT_STREQ(SCRIPT, state.last_alloc.file);
  EXPECT_EQ(42, state.last_alloc.line);

  p = oak_realloc(&a, p, 32, oak_source_loc_make(SCRIPT, 43));
  ASSERT_TRUE(p != null);
  EXPECT_STREQ(SCRIPT, state.last_realloc.file);
  EXPECT_EQ(43, state.last_realloc.line);

  oak_free(&a, p, oak_source_loc_make(SCRIPT, 44));
  EXPECT_STREQ(SCRIPT, state.last_free.file);
  EXPECT_EQ(44, state.last_free.line);
}

/*
 * The defect the API exists to fix. `helper` is the allocating layer; it must
 * report the location it was given rather than its own, so that a leak points
 * at whoever asked for the memory.
 */
static void* helper(oak_allocator_t* a, oak_source_loc_t at)
{
  return oak_alloc(a, 8, at);
}

UTEST(allocator, a_helper_reports_its_caller_not_itself)
{
  record_state_t state;
  oak_allocator_t a;
  const int caller_line = __LINE__;
  void* p;

  record_state_init(&state);
  a = record_allocator(&state);

  p = helper(&a, oak_source_loc_make(__FILE__, caller_line));
  ASSERT_TRUE(p != null);

  EXPECT_EQ(caller_line, state.last_alloc.line);
  oak_free(&a, p, OAK_HERE);
}

UTEST(allocator, oak_here_attributes_to_the_call_site)
{
  record_state_t state;
  oak_allocator_t a;
  void* p;
  int call_line;

  record_state_init(&state);
  a = record_allocator(&state);

  /* Kept adjacent: OAK_HERE must name the line it sits on. */
  call_line = __LINE__ + 1;
  p = oak_alloc(&a, 24, OAK_HERE);

  ASSERT_TRUE(p != null);
  EXPECT_EQ(call_line, state.last_alloc.line);
  EXPECT_STREQ(__FILE__, state.last_alloc.file);

  oak_free(&a, p, OAK_HERE);
}

UTEST(allocator, oak_here_matches_the_line_it_is_written_on)
{
  const int line = __LINE__ + 1;
  const oak_source_loc_t at = OAK_HERE;

  EXPECT_EQ(line, at.line);
  EXPECT_STREQ(__FILE__, at.file);
}

/* The payoff: a forwarded location is what the leak report names. */
UTEST(allocator, a_forwarded_location_is_what_the_leak_report_prints)
{
  oak_allocator_t a;
  oak_capture_t cap;
  char err[OAK_TEST_OUTPUT_MAX];
  int leaked;

  oak_tracking_allocator_init(&a);

  /* Leaked on purpose; shutdown below reclaims it while reporting. */
  ASSERT_TRUE(oak_alloc(&a, 48, oak_source_loc_make(SCRIPT, 42)) != null);

  oak_capture_begin(&cap, stderr);
  leaked = a.shutdown(&a);
  oak_capture_end(&cap, err, sizeof err);

  EXPECT_EQ(1, leaked);
  EXPECT_TRUE(oak_test_contains(err, "leaked memory: script.oak:42"));
}

UTEST(allocator, a_realloc_restamps_the_location)
{
  oak_allocator_t a;
  oak_capture_t cap;
  char err[OAK_TEST_OUTPUT_MAX];
  void* p;
  int leaked;

  oak_tracking_allocator_init(&a);

  p = oak_alloc(&a, 16, oak_source_loc_make(SCRIPT, 10));
  ASSERT_TRUE(p != null);
  ASSERT_TRUE(oak_realloc(&a, p, 64, oak_source_loc_make(SCRIPT, 20)) != null);

  oak_capture_begin(&cap, stderr);
  leaked = a.shutdown(&a);
  oak_capture_end(&cap, err, sizeof err);

  EXPECT_EQ(1, leaked);
  EXPECT_TRUE(oak_test_contains(err, "leaked memory: script.oak:20"));
  EXPECT_FALSE(oak_test_contains(err, "script.oak:10"));
}

/*
 * A bad free is attributed to where the free happened, which is the only
 * location that can point at the bug.
 *
 * The bad pointer is an interior one into a live allocation rather than a
 * genuine double free: the allocator reads a header behind whatever it is
 * given, and behind an interior pointer that header still lands inside memory
 * this test owns. A freed block would be a use-after-free, which is the thing
 * this suite is supposed to catch, not commit.
 */
UTEST(allocator, a_bad_free_reports_the_freeing_location)
{
  oak_allocator_t a;
  oak_capture_t cap;
  char err[OAK_TEST_OUTPUT_MAX];
  char* p;

  oak_tracking_allocator_init(&a);

  p = oak_alloc(&a, 1024, OAK_HERE);
  ASSERT_TRUE(p != null);

  oak_capture_begin(&cap, stderr);
  oak_free(&a, p + 512, oak_source_loc_make(SCRIPT, 99));
  oak_capture_end(&cap, err, sizeof err);

  EXPECT_TRUE(
      oak_test_contains(err, "memory signature mismatch: script.oak:99"));

  /* Nothing was released, so the real block still has to be. */
  oak_free(&a, p, OAK_HERE);
  EXPECT_EQ(0, a.shutdown(&a));
}

/* A null `file` is tolerated rather than dereferenced: the report says "?". */
UTEST(allocator, an_unknown_location_reports_a_placeholder)
{
  oak_allocator_t a;
  oak_capture_t cap;
  char err[OAK_TEST_OUTPUT_MAX];
  int leaked;

  oak_tracking_allocator_init(&a);
  ASSERT_TRUE(oak_alloc(&a, 8, oak_source_loc_make(null, 0)) != null);

  oak_capture_begin(&cap, stderr);
  leaked = a.shutdown(&a);
  oak_capture_end(&cap, err, sizeof err);

  EXPECT_EQ(1, leaked);
  EXPECT_TRUE(oak_test_contains(err, "leaked memory: ?:0"));
}
