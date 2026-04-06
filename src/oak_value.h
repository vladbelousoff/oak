#pragma once

#include "oak_log.h"
#include "oak_refcount.h"

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  OAK_VAL_BOOL,
  OAK_VAL_NUMBER,
  OAK_VAL_OBJ,
} oak_value_type_t;

typedef enum
{
  OAK_OBJ_CUSTOM,
  OAK_OBJ_STRING,
  OAK_OBJ_ARRAY,
  OAK_OBJ_MAP,
} oak_obj_type_t;

typedef struct oak_obj_t
{
  oak_obj_type_t type;
  oak_refcount_t refcount;
} oak_obj_t;

#define OAK_NUMBER_FLAG_FLOAT (1u << 0)

typedef struct
{
  unsigned flags;
  union
  {
    int integer;
    float floating;
  };
} oak_number_t;

typedef struct
{
  oak_obj_t obj;
  size_t length;
  uint32_t hash;
  char chars[];
} oak_obj_string_t;

typedef struct
{
  oak_value_type_t type;
  union
  {
    int boolean;
    oak_number_t number;
    oak_obj_t* obj;
  } as;
} oak_value_t;

#define OAK_VALUE_BOOL(_b)                                                     \
  ((oak_value_t){                                                              \
      .type = OAK_VAL_BOOL,                                                    \
      .as.boolean = (_b),                                                      \
  })

#define OAK_VALUE_I32(_i)                                                      \
  ((oak_value_t){                                                              \
      .type = OAK_VAL_NUMBER,                                                  \
      .as.number =                                                             \
          (oak_number_t){                                                      \
              .integer = (_i),                                                 \
          },                                                                   \
  })

#define OAK_VALUE_F32(_f)                                                      \
  ((oak_value_t){                                                              \
      .type = OAK_VAL_NUMBER,                                                  \
      .as.number =                                                             \
          (oak_number_t){                                                      \
              .flags = OAK_NUMBER_FLAG_FLOAT,                                  \
              .floating = (_f),                                                \
          },                                                                   \
  })

#define OAK_VALUE_OBJ(_obj)                                                    \
  ((oak_value_t){                                                              \
      .type = OAK_VAL_OBJ,                                                     \
      .as.obj = (oak_obj_t*)(_obj),                                            \
  })

oak_obj_string_t* oak_make_string(const char* chars, size_t length);

oak_obj_string_t* oak_string_concat(const oak_obj_string_t* a,
                                    const oak_obj_string_t* b);

static inline int oak_is_bool(const oak_value_t value)
{
  return value.type == OAK_VAL_BOOL;
}

static inline int oak_is_number(const oak_value_t value)
{
  return value.type == OAK_VAL_NUMBER;
}

static inline int oak_is_obj(const oak_value_t value)
{
  return value.type == OAK_VAL_OBJ;
}

static inline int oak_is_string(const oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_STRING;
}

static inline int oak_is_i32(const oak_value_t value)
{
  return oak_is_number(value) &&
         !(value.as.number.flags & OAK_NUMBER_FLAG_FLOAT);
}

static inline int oak_is_f32(const oak_value_t value)
{
  return oak_is_number(value) && value.as.number.flags & OAK_NUMBER_FLAG_FLOAT;
}

static inline int oak_as_bool(const oak_value_t value)
{
  oak_assert(oak_is_bool(value));
  return value.as.boolean;
}

static inline int oak_as_i32(const oak_value_t value)
{
  oak_assert(oak_is_i32(value));
  return value.as.number.integer;
}

static inline float oak_as_f32(const oak_value_t value)
{
  oak_assert(oak_is_f32(value));
  return value.as.number.floating;
}

static inline oak_obj_t* oak_as_obj(const oak_value_t value)
{
  oak_assert(oak_is_obj(value));
  return value.as.obj;
}

static inline oak_obj_string_t* oak_as_string(const oak_value_t value)
{
  oak_assert(oak_is_string(value));
  return (oak_obj_string_t*)value.as.obj;
}

static inline char* oak_as_cstring(const oak_value_t value)
{
  return oak_as_string(value)->chars;
}

int oak_is_truthy(oak_value_t value);
int oak_value_equal(oak_value_t a, oak_value_t b);

void oak_obj_incref(oak_obj_t* obj);
void oak_obj_decref(oak_obj_t* obj);

void oak_value_incref(oak_value_t value);
void oak_value_decref(oak_value_t value);

void oak_value_print(oak_value_t value);
