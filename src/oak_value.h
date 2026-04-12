#pragma once

#include "oak_log.h"
#include "oak_refcount.h"

#include <stddef.h>
#include <stdint.h>

enum oak_value_type_t
{
  OAK_VAL_BOOL,
  OAK_VAL_NUMBER,
  OAK_VAL_OBJ,
};

enum oak_obj_type_t
{
  OAK_OBJ_CUSTOM,
  OAK_OBJ_STRING,
  OAK_OBJ_ARRAY,
  OAK_OBJ_MAP,
  OAK_OBJ_FN,
  OAK_OBJ_NATIVE_FN,
};

struct oak_obj_t
{
  enum oak_obj_type_t type;
  struct oak_refcount_t refcount;
};

#define OAK_NUMBER_FLAG_FLOAT (1u << 0)

struct oak_number_t
{
  unsigned flags;
  union
  {
    int integer;
    float floating;
  };
};

struct oak_obj_string_t
{
  struct oak_obj_t obj;
  size_t length;
  uint32_t hash;
  char chars[];
};

struct oak_obj_fn_t
{
  struct oak_obj_t obj;
  size_t code_offset;
  int arity;
};

struct oak_value_t
{
  enum oak_value_type_t type;
  union
  {
    int boolean;
    struct oak_number_t number;
    struct oak_obj_t* obj;
  } as;
};

enum oak_fn_call_result_t
{
  OAK_FN_CALL_OK = 0,
  OAK_FN_CALL_RUNTIME_ERROR,
};

/* Native (C) callable: returns OAK_FN_CALL_OK on success. */
typedef enum oak_fn_call_result_t (*oak_native_fn_t)(void* vm,
                                                     const struct oak_value_t* args,
                                                     int argc,
                                                     struct oak_value_t* out_result);

struct oak_obj_native_fn_t
{
  struct oak_obj_t obj;
  oak_native_fn_t fn;
  int arity;
  /* Debug label (e.g. registered name); not owned, may be NULL. */
  const char* name;
};

#define OAK_VALUE_BOOL(_b)                                                     \
  ((struct oak_value_t){                                                       \
      .type = OAK_VAL_BOOL,                                                    \
      .as.boolean = (_b),                                                      \
  })

#define OAK_VALUE_I32(_i)                                                      \
  ((struct oak_value_t){                                                       \
      .type = OAK_VAL_NUMBER,                                                  \
      .as.number =                                                             \
          (struct oak_number_t){                                               \
              .integer = (_i),                                                 \
          },                                                                   \
  })

#define OAK_VALUE_F32(_f)                                                      \
  ((struct oak_value_t){                                                       \
      .type = OAK_VAL_NUMBER,                                                  \
      .as.number =                                                             \
          (struct oak_number_t){                                               \
              .flags = OAK_NUMBER_FLAG_FLOAT,                                  \
              .floating = (_f),                                                \
          },                                                                   \
  })

#define OAK_VALUE_OBJ(_obj)                                                    \
  ((struct oak_value_t){                                                       \
      .type = OAK_VAL_OBJ,                                                     \
      .as.obj = (struct oak_obj_t*)(_obj),                                     \
  })

struct oak_obj_string_t* oak_make_string(const char* chars, size_t length);

struct oak_obj_string_t* oak_string_concat(const struct oak_obj_string_t* a,
                                           const struct oak_obj_string_t* b);

struct oak_obj_fn_t* oak_make_fn(size_t code_offset, int arity);

struct oak_obj_native_fn_t* oak_make_native_fn(oak_native_fn_t fn,
                                                 int arity,
                                                 const char* name);

int oak_native_fn_format(char* buf,
                         size_t size,
                         const struct oak_obj_native_fn_t* native);

static inline int oak_is_bool(const struct oak_value_t value)
{
  return value.type == OAK_VAL_BOOL;
}

static inline int oak_is_number(const struct oak_value_t value)
{
  return value.type == OAK_VAL_NUMBER;
}

static inline int oak_is_obj(const struct oak_value_t value)
{
  return value.type == OAK_VAL_OBJ;
}

static inline int oak_is_string(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_STRING;
}

static inline int oak_is_fn(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_FN;
}

static inline int oak_is_native_fn(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_NATIVE_FN;
}

static inline int oak_is_i32(const struct oak_value_t value)
{
  return oak_is_number(value) &&
         !(value.as.number.flags & OAK_NUMBER_FLAG_FLOAT);
}

static inline int oak_is_f32(const struct oak_value_t value)
{
  return oak_is_number(value) && value.as.number.flags & OAK_NUMBER_FLAG_FLOAT;
}

static inline int oak_as_bool(const struct oak_value_t value)
{
  oak_assert(oak_is_bool(value));
  return value.as.boolean;
}

static inline int oak_as_i32(const struct oak_value_t value)
{
  oak_assert(oak_is_i32(value));
  return value.as.number.integer;
}

static inline float oak_as_f32(const struct oak_value_t value)
{
  oak_assert(oak_is_f32(value));
  return value.as.number.floating;
}

static inline struct oak_obj_t* oak_as_obj(const struct oak_value_t value)
{
  oak_assert(oak_is_obj(value));
  return value.as.obj;
}

static inline struct oak_obj_string_t*
oak_as_string(const struct oak_value_t value)
{
  oak_assert(oak_is_string(value));
  return (struct oak_obj_string_t*)value.as.obj;
}

static inline struct oak_obj_fn_t* oak_as_fn(const struct oak_value_t value)
{
  oak_assert(oak_is_fn(value));
  return (struct oak_obj_fn_t*)value.as.obj;
}

static inline struct oak_obj_native_fn_t*
oak_as_native_fn(const struct oak_value_t value)
{
  oak_assert(oak_is_native_fn(value));
  return (struct oak_obj_native_fn_t*)value.as.obj;
}

static inline char* oak_as_cstring(const struct oak_value_t value)
{
  return oak_as_string(value)->chars;
}

int oak_is_truthy(struct oak_value_t value);
int oak_value_equal(struct oak_value_t a, struct oak_value_t b);

void oak_obj_incref(struct oak_obj_t* obj);
void oak_obj_decref(struct oak_obj_t* obj);

void oak_value_incref(struct oak_value_t value);
void oak_value_decref(struct oak_value_t value);

void oak_value_print(struct oak_value_t value);

enum oak_fn_call_result_t oak_builtin_print(void* vm,
                                            const struct oak_value_t* args,
                                            int argc,
                                            struct oak_value_t* out_result);
