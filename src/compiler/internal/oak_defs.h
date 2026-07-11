#pragma once

#define OAK_MAX_LOCALS 256

#define OAK_LOC_SYNTHETIC ((struct oak_code_loc_t){ .line = 0, .column = 1 })

/* Bail out of the current (void) function on the first compilation error. */
#define CHECK_ERROR(c)                                                         \
  do                                                                           \
  {                                                                            \
    if ((c)->has_error)                                                        \
      return;                                                                  \
  } while (0)

#define OAK_MAX_ARRAY_METHODS          8
#define OAK_MAX_MAP_METHODS            8
#define OAK_MAX_STRING_METHODS         16
#define OAK_MAX_BOOL_METHODS           4
#define OAK_MAX_NUMBER_METHODS         4
#define OAK_MAX_RECORD_BUILTIN_METHODS 4
