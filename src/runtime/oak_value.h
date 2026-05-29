#pragma once

#include "oak_export.h"
#include "oak_log.h"
#include "oak_refcount.h"

#include "oak_types.h"

#include <string.h>

/* ===== Tagged-union value representation =====
 *
 * Every Oak value is a 16-byte tag + payload.  Immediates (i32, f32, bool,
 * none) are stored inline; object values carry a full native pointer.
 */

/* Runtime tag for the payload of an oak_value_t.
 *
 * OAK_TAG_WEAK is a non-owning reference to an oak_obj_t: it bumps the
 * object's weak_refcount but not its strong refcount, and on dereference
 * the VM upgrades it to a strong reference (or to NONE if the target has
 * already been freed).  Weak references exist to break ownership cycles
 * — see the `weak` type modifier in the language tour. */
enum oak_value_tag_t
{
  OAK_TAG_I32,
  OAK_TAG_F32,
  OAK_TAG_BOOL,
  OAK_TAG_NONE,
  OAK_TAG_OBJ,
  OAK_TAG_WEAK,
  /* Inline native value type (OAK_BIND_TYPE_VALUE): an opaque pointer/handle
   * payload stored directly in `as.obj`.  Not an oak_obj_t — it is never
   * dereferenced, refcounted, or freed by the runtime.  Copies are bitwise. */
  OAK_TAG_NATIVE,
};

enum oak_value_type_t
{
  OAK_VAL_BOOL,
  OAK_VAL_NUMBER,
  OAK_VAL_OBJ,
  OAK_VAL_WEAK_OBJ,
  OAK_VAL_NONE,
};

enum oak_obj_type_t
{
  OAK_OBJ_STRING,
  OAK_OBJ_ARRAY,
  OAK_OBJ_MAP,
  OAK_OBJ_FN,
  OAK_OBJ_NATIVE_FN,
  OAK_OBJ_RECORD,
  OAK_OBJ_NATIVE_RECORD,
  OAK_OBJ_TRAIT_OBJECT,
};

struct oak_allocator_t;

struct oak_obj_t
{
  enum oak_obj_type_t type;
  struct oak_refcount_t refcount;
  struct oak_refcount_t weak_refcount;
  struct oak_allocator_t* allocator;
};

struct oak_value_t
{
  enum oak_value_tag_t tag;
  union
  {
    i32 i;
    float f;
    int b;
    struct oak_obj_t* obj;
  } as;
};

/* ===== Utilities ===== */

static inline u32 oak_f32_to_bits(const float f)
{
  u32 b;
  memcpy(&b, &f, sizeof(b));
  return b;
}

/* ===== Value constructors ===== */

static inline struct oak_value_t oak_value_i32(const i32 i)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_I32;
  value.as.i = i;
  return value;
}

static inline struct oak_value_t oak_value_f32(const float f)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_F32;
  value.as.f = f;
  return value;
}

static inline struct oak_value_t oak_value_bool(const int b)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_BOOL;
  value.as.b = b ? 1 : 0;
  return value;
}

static inline struct oak_value_t oak_value_none(void)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_NONE;
  return value;
}

static inline struct oak_value_t oak_value_obj(struct oak_obj_t* obj)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_OBJ;
  value.as.obj = obj;
  return value;
}

static inline struct oak_value_t oak_value_weak_obj(struct oak_obj_t* obj)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_WEAK;
  value.as.obj = obj;
  return value;
}

static inline struct oak_value_t oak_value_native(void* payload)
{
  struct oak_value_t value;
  value.tag = OAK_TAG_NATIVE;
  value.as.obj = (struct oak_obj_t*)payload;
  return value;
}

#define OAK_VALUE_I32(_i) oak_value_i32(_i)
#define OAK_VALUE_F32(_f) oak_value_f32(_f)
#define OAK_VALUE_BOOL(_b) oak_value_bool(_b)
#define OAK_VALUE_NONE oak_value_none()
#define OAK_VALUE_OBJ(_obj) oak_value_obj((struct oak_obj_t*)(_obj))
#define OAK_VALUE_WEAK_OBJ(_obj) oak_value_weak_obj((struct oak_obj_t*)(_obj))
#define OAK_VALUE_NATIVE(_p) oak_value_native((void*)(_p))

/* ===== Type predicates ===== */

static inline int oak_is_bool(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_BOOL;
}

static inline int oak_is_number(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_I32 || value.tag == OAK_TAG_F32;
}

static inline int oak_is_i32(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_I32;
}

static inline int oak_is_f32(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_F32;
}

static inline int oak_is_none(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_NONE;
}

static inline int oak_is_weak_obj(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_WEAK;
}

static inline int oak_is_native_value(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_NATIVE;
}

static inline void* oak_as_native_value(const struct oak_value_t value)
{
  oak_assert(oak_is_native_value(value));
  return (void*)value.as.obj;
}

static inline struct oak_obj_t* oak_val_obj_ptr(const struct oak_value_t value)
{
  return value.as.obj;
}

static inline int oak_is_obj(const struct oak_value_t value)
{
  if (value.tag == OAK_TAG_OBJ)
    return 1;
  if (value.tag == OAK_TAG_WEAK)
    return value.as.obj->refcount.count != 0;
  return 0;
}

static inline int oak_is_expired_weak(const struct oak_value_t value)
{
  return value.tag == OAK_TAG_WEAK &&
         value.as.obj->refcount.count == 0;
}

static inline int oak_is_none_like(const struct oak_value_t value)
{
  return oak_is_none(value) || oak_is_expired_weak(value);
}

static inline int oak_is_string(const struct oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_STRING;
}

static inline int oak_is_fn(const struct oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_FN;
}

static inline int oak_is_native_fn(const struct oak_value_t value)
{
  return oak_is_obj(value) &&
         oak_val_obj_ptr(value)->type == OAK_OBJ_NATIVE_FN;
}

static inline int oak_is_array(const struct oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_ARRAY;
}

static inline int oak_is_map(const struct oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_MAP;
}

static inline int oak_is_record(const struct oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_RECORD;
}

static inline int oak_is_native_record(const struct oak_value_t value)
{
  return oak_is_obj(value) &&
         oak_val_obj_ptr(value)->type == OAK_OBJ_NATIVE_RECORD;
}

static inline int oak_is_trait_object(const struct oak_value_t value)
{
  return oak_is_obj(value) &&
         oak_val_obj_ptr(value)->type == OAK_OBJ_TRAIT_OBJECT;
}

/* ===== Value extractors ===== */

static inline int oak_as_bool(const struct oak_value_t value)
{
  oak_assert(oak_is_bool(value));
  return value.as.b;
}

static inline int oak_as_i32(const struct oak_value_t value)
{
  oak_assert(oak_is_i32(value));
  return (int)value.as.i;
}

static inline float oak_as_f32(const struct oak_value_t value)
{
  oak_assert(oak_is_f32(value));
  return value.as.f;
}

static inline struct oak_obj_t* oak_as_obj(const struct oak_value_t value)
{
  oak_assert(oak_is_obj(value));
  return value.as.obj;
}

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
  u16 module_id;
  const char* name;
  struct oak_attr_hook_entry_t* attr_hooks;
  int attr_hook_count;
};

struct oak_value_t;
struct oak_native_ctx_t;

enum oak_fn_call_result_t
{
  OAK_FN_CALL_OK = 0,
  OAK_FN_CALL_RUNTIME_ERROR,
};

typedef enum oak_fn_call_result_t (*oak_attr_runtime_cb_t)(
    struct oak_native_ctx_t* ctx,
    const char* fn_name,
    const struct oak_value_t* args,
    int argc,
    void* user_data);

struct oak_attr_hook_entry_t
{
  oak_attr_runtime_cb_t cb;
  void* ud;
};

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
  const char* name;
  struct oak_attr_hook_entry_t* attr_hooks;
  int attr_hook_count;
};

struct oak_obj_array_t
{
  struct oak_obj_t obj;
  usize length;
  usize capacity;
  struct oak_value_t* items;
};

struct oak_map_entry_t
{
  struct oak_value_t key;
  struct oak_value_t value;
};

#define MAP_HT_EMPTY     ((usize) - 1)
#define MAP_HT_TOMBSTONE ((usize) - 2)

struct oak_obj_map_t
{
  struct oak_obj_t obj;
  usize length;
  usize capacity;
  struct oak_map_entry_t* entries;
  usize ht_capacity;
  usize* ht;
};

struct oak_obj_record_t
{
  struct oak_obj_t obj;
  const char* type_name;
  int field_count;
  const char* const* field_name_ptrs;
  void* field_name_storage;
  struct oak_value_t fields[];
};

struct oak_bind_type_t;

struct oak_obj_native_record_t
{
  struct oak_obj_t obj;
  void* instance;
  const struct oak_bind_type_t* type;
};

struct oak_obj_trait_object_t
{
  struct oak_obj_t obj;
  struct oak_value_t value;
  struct oak_obj_array_t* vtable;
};

struct oak_vm_t;

struct oak_native_ctx_t
{
  struct oak_vm_t* vm;
  struct oak_allocator_t* allocator;
};

/* ===== Typed cast helpers ===== */

static inline struct oak_obj_string_t*
oak_as_string(const struct oak_value_t value)
{
  oak_assert(oak_is_string(value));
  return (struct oak_obj_string_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_fn_t* oak_as_fn(const struct oak_value_t value)
{
  oak_assert(oak_is_fn(value));
  return (struct oak_obj_fn_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_native_fn_t*
oak_as_native_fn(const struct oak_value_t value)
{
  oak_assert(oak_is_native_fn(value));
  return (struct oak_obj_native_fn_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_array_t*
oak_as_array(const struct oak_value_t value)
{
  oak_assert(oak_is_array(value));
  return (struct oak_obj_array_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_map_t* oak_as_map(const struct oak_value_t value)
{
  oak_assert(oak_is_map(value));
  return (struct oak_obj_map_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_record_t*
oak_as_record(const struct oak_value_t value)
{
  oak_assert(oak_is_record(value));
  return (struct oak_obj_record_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_native_record_t*
oak_as_native_record(const struct oak_value_t value)
{
  oak_assert(oak_is_native_record(value));
  return (struct oak_obj_native_record_t*)oak_val_obj_ptr(value);
}

static inline struct oak_obj_trait_object_t*
oak_as_trait_object(const struct oak_value_t value)
{
  oak_assert(oak_is_trait_object(value));
  return (struct oak_obj_trait_object_t*)oak_val_obj_ptr(value);
}

static inline char* oak_as_cstring(const struct oak_value_t value)
{
  return oak_as_string(value)->chars;
}

/* ===== Reference counting ===== */

OAK_API void oak_obj_incref(struct oak_obj_t* obj);
OAK_API void oak_obj_decref(struct oak_obj_t* obj);
OAK_API void oak_weak_decref(struct oak_obj_t* obj);

static inline void oak_value_incref(const struct oak_value_t value)
{
  if (value.tag == OAK_TAG_OBJ)
    oak_obj_incref(value.as.obj);
  else if (value.tag == OAK_TAG_WEAK)
    oak_refcount_inc(&value.as.obj->weak_refcount);
}

static inline void oak_value_decref(const struct oak_value_t value)
{
  if (value.tag == OAK_TAG_OBJ)
    oak_obj_decref(value.as.obj);
  else if (value.tag == OAK_TAG_WEAK)
    oak_weak_decref(value.as.obj);
}

static inline struct oak_value_t
oak_value_weaken(const struct oak_value_t value)
{
  if (oak_is_obj(value))
    return OAK_VALUE_WEAK_OBJ(value.as.obj);
  return value;
}

/* ===== Public API ===== */

OAK_API int oak_is_truthy(struct oak_value_t value);
OAK_API int oak_value_equal(struct oak_value_t a, struct oak_value_t b);

OAK_API struct oak_obj_string_t* oak_string_new(struct oak_allocator_t* a,
                                                const char* chars,
                                                usize length);

OAK_API struct oak_obj_string_t*
oak_string_concat(struct oak_allocator_t* a,
                  const struct oak_obj_string_t* s1,
                  const struct oak_obj_string_t* s2);

OAK_API struct oak_obj_fn_t* oak_fn_new(struct oak_allocator_t* a,
                                        usize code_offset,
                                        int arity,
                                        u16 module_id);

OAK_API struct oak_obj_native_fn_t*
oak_native_fn_new(struct oak_allocator_t* a,
                  oak_native_fn_t fn,
                  int arity,
                  const char* name);

OAK_API struct oak_obj_array_t* oak_array_new(struct oak_allocator_t* a);
OAK_API void oak_array_push(struct oak_obj_array_t* arr,
                            struct oak_value_t value);

OAK_API struct oak_obj_record_t* oak_record_new(
    struct oak_allocator_t* a,
    int field_count,
    const char* type_name,
    const char* const* field_names,
    const usize* field_name_len);

OAK_API struct oak_obj_native_record_t*
oak_obj_native_record_new(struct oak_allocator_t* a,
                          const struct oak_bind_type_t* type,
                          void* instance);

OAK_API struct oak_obj_trait_object_t*
oak_trait_object_new(struct oak_allocator_t* a,
                     struct oak_value_t value,
                     struct oak_obj_array_t* vtable);

OAK_API struct oak_obj_map_t* oak_map_new(struct oak_allocator_t* a);
OAK_API int oak_map_get(const struct oak_obj_map_t* map,
                        struct oak_value_t key,
                        struct oak_value_t* out);
OAK_API int oak_map_set(struct oak_obj_map_t* map,
                        struct oak_value_t key,
                        struct oak_value_t value);
OAK_API int oak_map_has(const struct oak_obj_map_t* map,
                        struct oak_value_t key);
OAK_API int oak_map_delete(struct oak_obj_map_t* map, struct oak_value_t key);
OAK_API struct oak_value_t oak_map_key_at(const struct oak_obj_map_t* map,
                                          usize index);
OAK_API struct oak_value_t oak_map_value_at(const struct oak_obj_map_t* map,
                                            usize index);

OAK_API int oak_native_fn_format(char* buf,
                                 usize size,
                                 const struct oak_obj_native_fn_t* native);

OAK_API struct oak_obj_string_t* oak_value_to_string(
    struct oak_allocator_t* allocator, struct oak_value_t value);

OAK_API void oak_value_println(struct oak_allocator_t* allocator,
                               struct oak_value_t value);

OAK_API int oak_value_snprint_repr(char* buf,
                                   usize size,
                                   struct oak_value_t value);

OAK_API struct oak_obj_string_t*
oak_string_from_value_repr(struct oak_allocator_t* allocator,
                           struct oak_value_t value);
