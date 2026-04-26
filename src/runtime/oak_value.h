#pragma once

#include "oak_log.h"
#include "oak_refcount.h"

#include "oak_types.h"

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
  OAK_OBJ_RECORD,
  /* A native C value wrapped as an Oak value.  The underlying instance
   * pointer is not owned by Oak; the type descriptor pointer is borrowed from
   * the oak_native_type_t that was registered with oak_bind_type(). */
  OAK_OBJ_NATIVE_RECORD,
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
  usize length;
  u32 hash;
  char chars[];
};

struct oak_obj_fn_t
{
  struct oak_obj_t obj;
  usize code_offset;
  int arity;
};

struct oak_value_t;

struct oak_obj_array_t
{
  struct oak_obj_t obj;
  usize length;
  usize capacity;
  struct oak_value_t* items;
};

struct oak_map_entry_t;

/* Insertion-ordered map backed by a dense `entries[]` array for O(1)
 * positional access and an open-addressing hash table (`ht[]`) for O(1)
 * key lookup.  `ht[i]` holds the index into `entries[]` for a live entry,
 * MAP_HT_EMPTY when the slot is unused, or MAP_HT_TOMBSTONE when an entry
 * was deleted.  `ht_capacity` is always a power of two >= 8. */
#define MAP_HT_EMPTY     ((usize) - 1)
#define MAP_HT_TOMBSTONE ((usize) - 2)

struct oak_obj_map_t
{
  struct oak_obj_t obj;
  usize length;   /* live entries in entries[] */
  usize capacity; /* allocated slots in entries[] */
  struct oak_map_entry_t* entries;
  usize ht_capacity; /* slots in ht[] (power of 2) */
  usize* ht;         /* open-addressing hash table */
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

/* User-defined record instance. Fields are stored densely in declaration
 * order. The record's compile-time type is not tracked at runtime; field
 * lookup is resolved to a fixed index by the compiler. */
struct oak_obj_record_t
{
  struct oak_obj_t obj;
  /* Borrowed pointer to the record's name (lives for the lifetime of the
   * source buffer); used for diagnostics only. May be null. */
  const char* type_name;
  int field_count;
  struct oak_value_t fields[];
};

/* Forward declaration — full definition lives in oak_bind.h. */
struct oak_native_type_t;

/* A native C instance wrapped as an Oak heap object.  `type` is borrowed for
 * the binding lifetime.  When the refcount reaches zero, `type->destroy_instance`
 * runs on non-NULL `instance` if registered; then the wrapper is freed. */
struct oak_obj_native_record_t
{
  struct oak_obj_t obj;
  void* instance;
  const struct oak_native_type_t*
      type; /* borrowed; lives for binding lifetime */
};

enum oak_fn_call_result_t
{
  OAK_FN_CALL_OK = 0,
  OAK_FN_CALL_RUNTIME_ERROR,
};

struct oak_vm_t;

/* Passed to every native (C) callback: VM handle for runtime helpers. */
struct oak_native_ctx_t
{
  struct oak_vm_t* vm;
};

/* Native (C) callable: returns OAK_FN_CALL_OK on success. */
typedef enum oak_fn_call_result_t (*oak_native_fn_t)(
    struct oak_native_ctx_t* ctx,
    const struct oak_value_t* args,
    int argc,
    struct oak_value_t* out_result);

struct oak_obj_native_fn_t
{
  struct oak_obj_t obj;
  oak_native_fn_t fn;
  int arity;
  /* Debug label (e.g. registered name); not owned, may be null. */
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

struct oak_obj_string_t* oak_string_new(const char* chars, usize length);

struct oak_obj_string_t* oak_string_concat(const struct oak_obj_string_t* a,
                                           const struct oak_obj_string_t* b);

struct oak_obj_fn_t* oak_fn_new(usize code_offset, int arity);

struct oak_obj_native_fn_t* oak_native_fn_new(oak_native_fn_t fn,
                                              int arity,
                                              const char* name);

struct oak_obj_array_t* oak_array_new(void);
void oak_array_push(struct oak_obj_array_t* arr, struct oak_value_t value);

struct oak_obj_record_t* oak_record_new(int field_count, const char* type_name);

struct oak_obj_native_record_t*
oak_obj_native_record_new(const struct oak_native_type_t* type, void* instance);

struct oak_obj_map_t* oak_map_new(void);
/* Returns 1 and writes the value into *out if found; 0 otherwise. */
int oak_map_get(const struct oak_obj_map_t* map,
                struct oak_value_t key,
                struct oak_value_t* out);
/* Inserts or replaces the value for `key`. Increments refcounts as needed. */
void oak_map_set(struct oak_obj_map_t* map,
                 struct oak_value_t key,
                 struct oak_value_t value);
int oak_map_has(const struct oak_obj_map_t* map, struct oak_value_t key);
/* Removes the entry with the given key. Returns 1 if removed, 0 otherwise. */
int oak_map_delete(struct oak_obj_map_t* map, struct oak_value_t key);
struct oak_value_t oak_map_key_at(const struct oak_obj_map_t* map, usize index);
struct oak_value_t oak_map_value_at(const struct oak_obj_map_t* map,
                                    usize index);

int oak_native_fn_format(char* buf,
                         usize size,
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

static inline int oak_is_array(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_ARRAY;
}

static inline int oak_is_map(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_MAP;
}

static inline int oak_is_record(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_RECORD;
}

static inline int oak_is_native_record(const struct oak_value_t value)
{
  return oak_is_obj(value) && value.as.obj->type == OAK_OBJ_NATIVE_RECORD;
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

static inline struct oak_obj_array_t*
oak_as_array(const struct oak_value_t value)
{
  oak_assert(oak_is_array(value));
  return (struct oak_obj_array_t*)value.as.obj;
}

static inline struct oak_obj_map_t* oak_as_map(const struct oak_value_t value)
{
  oak_assert(oak_is_map(value));
  return (struct oak_obj_map_t*)value.as.obj;
}

static inline struct oak_obj_record_t*
oak_as_record(const struct oak_value_t value)
{
  oak_assert(oak_is_record(value));
  return (struct oak_obj_record_t*)value.as.obj;
}

static inline struct oak_obj_native_record_t*
oak_as_native_record(const struct oak_value_t value)
{
  oak_assert(oak_is_native_record(value));
  return (struct oak_obj_native_record_t*)value.as.obj;
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

void oak_value_println(struct oak_value_t value);
