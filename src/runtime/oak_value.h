#pragma once

#include "oak_export.h"
#include "oak_log.h"
#include "oak_refcount.h"

#include "oak_types.h"

#include <string.h>

/* ===== Packed 8-byte value representation =====
 *
 * Every Oak value is a single 64-bit word: a 3-bit tag in the low bits plus
 * a payload.  Immediates (i32, f32, bool) keep their payload in the high 32
 * bits; handles and inline native values carry a 61-bit payload; object
 * references carry an object-table id, a slot index into that table, and a
 * nonce that detects stale weak references.
 *
 * Word layout (bit 0 = least significant):
 *   [2:0]          tag (oak_value_tag_t)
 *   I32/F32/BOOL:  payload in [63:32]
 *   HANDLE/NATIVE: payload in [63:3]
 *   OBJ/WEAK:      slot index in [31:3], table id in [39:32],
 *                  nonce in [63:40]
 */

/* Runtime tag for the payload of an oak_value_t.
 *
 * OAK_TAG_WEAK is a non-owning reference to an oak_obj_t: it carries the
 * same slot index + nonce as a strong reference but never touches the
 * refcount.  When the object dies its table slot's nonce is bumped, so a
 * stale weak reference is detected by a nonce mismatch and reads as none;
 * on dereference the VM upgrades a live weak to a strong reference.  Weak
 * references exist to break ownership cycles — see the `weak` type
 * modifier in the language tour. */
enum oak_value_tag_t
{
  OAK_TAG_I32,
  OAK_TAG_F32,
  OAK_TAG_BOOL,
  OAK_TAG_NONE,
  /* Inline opaque handle for native value types (e.g. an ECS entity id).
   * Stored by value in the payload — never heap-allocated and never
   * refcounted.  Only 61 payload bits fit next to the tag: constructing a
   * handle with any of the top 3 bits set asserts in debug builds. */
  OAK_TAG_HANDLE,
  /* The only refcounted tag: incref/decref act exclusively on OAK_TAG_OBJ
   * values; every other tag is a trivially-copyable immediate. */
  OAK_TAG_OBJ,
  OAK_TAG_WEAK,
  /* Inline native value type (OAK_BIND_TYPE_VALUE): an opaque pointer
   * payload.  Not an oak_obj_t — it is never dereferenced, refcounted, or
   * freed by the runtime.  Copies are bitwise. */
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
  /* This object's slot in its object table (oak_obj_tables[table_id]). */
  u32 slot_index;
  u32 table_id;
  struct oak_allocator_t* allocator;
};

/* ===== Object tables =====
 *
 * Object values do not carry the oak_obj_t pointer; they carry a table id,
 * a slot index into that table, and the slot's nonce at the time the
 * reference was made.  A slot's nonce is bumped when its object dies, so an
 * outstanding weak reference resolves to null (nonce mismatch) instead of
 * dangling — even if the slot has since been reused by a new object.
 * Strong references pin the object alive, so their nonce always matches and
 * they resolve with a plain index load.
 *
 * Tables live in a fixed process-wide registry so that value accessors can
 * resolve any object without a context parameter.  Table 0 is the shared
 * table: it holds objects created outside a VM (chunk constants built by
 * the compiler, embedder-created values) and is never recycled.  Every VM
 * acquires its own table in oak_vm_init and detaches it in oak_vm_free;
 * objects created while that VM executes land in its table (routed through
 * a thread-local current-table id scoped by the VM entry points).  A
 * detached table is recycled — its slot array freed and its registry entry
 * reusable — once the last object in it dies; fresh slots then start at a
 * nonce floor above every nonce the previous incarnation issued, so stale
 * weak references cannot alias across recycles.
 *
 * Like the refcounts, the registry is not synchronized; multi-threaded
 * object creation/destruction requires external locking. */

struct oak_obj_slot_t
{
  struct oak_obj_t* obj; /* null while the slot is free */
  u32 nonce;             /* wraps at 24 bits; bumped when the object dies */
  u32 next_free;         /* freelist link, meaningful only while free */
};

enum oak_obj_table_state_t
{
  OAK_OBJ_TABLE_FREE,
  OAK_OBJ_TABLE_ACTIVE,   /* owned by a VM (or table 0, owned by the process) */
  OAK_OBJ_TABLE_DETACHED, /* owner gone; recycled once live_count reaches 0 */
};

struct oak_obj_table_t
{
  struct oak_obj_slot_t* slots;
  u32 capacity;
  u32 free_head;   /* OAK_OBJ_SLOT_NONE when the freelist is empty */
  u32 live_count;  /* occupied slots; gates recycling of detached tables */
  u32 nonce_floor; /* starting nonce for slots of the next incarnation */
  u8 state;        /* enum oak_obj_table_state_t */
};

#define OAK_OBJ_SLOT_NONE   0xFFFFFFFFu
#define OAK_OBJ_TABLE_COUNT 256u

OAK_API extern struct oak_obj_table_t oak_obj_tables[/*OAK_OBJ_TABLE_COUNT*/];

/* Reserve a table for a new owner (a VM).  Returns 0 — the shared,
 * never-recycled table — when all entries are taken. */
OAK_API u32 oak_obj_table_acquire(void);

/* Declare the owner gone.  The table is recycled as soon as no object in it
 * remains alive.  No-op for table 0. */
OAK_API void oak_obj_table_detach(u32 table_id);

/* Route objects created on this thread into `table_id`; returns the
 * previous current table so callers can scope and restore it.  The VM run
 * entry points wrap execution with this. */
OAK_API u32 oak_obj_table_set_current(u32 table_id);

struct oak_value_t
{
  /* Packed tag + payload — see the layout comment at the top of the file.
   * Access only through the constructors/predicates/extractors below. */
  u64 bits;
};

#define OAK_VALUE_TAG_MASK  ((u64)0x7u)
#define OAK_OBJ_INDEX_MASK  0x1FFFFFFFu /* 29 bits */
#define OAK_OBJ_TABLE_SHIFT 32u
#define OAK_OBJ_TABLE_MASK  0xFFu /* 8 bits */
#define OAK_OBJ_NONCE_SHIFT 40u
#define OAK_OBJ_NONCE_MASK  0xFFFFFFu /* 24 bits */

/* ===== Utilities ===== */

static inline u32 oak_f32_to_bits(const float f)
{
  u32 b;
  memcpy(&b, &f, sizeof(b));
  return b;
}

static inline float oak_f32_from_bits(const u32 b)
{
  float f;
  memcpy(&f, &b, sizeof(f));
  return f;
}

static inline enum oak_value_tag_t oak_value_tag(const struct oak_value_t value)
{
  return (enum oak_value_tag_t)(value.bits & OAK_VALUE_TAG_MASK);
}

/* ===== Value constructors ===== */

static inline struct oak_value_t oak_value_i32(const i32 i)
{
  struct oak_value_t value;
  value.bits = ((u64)(u32)i << 32u) | OAK_TAG_I32;
  return value;
}

static inline struct oak_value_t oak_value_f32(const float f)
{
  struct oak_value_t value;
  value.bits = ((u64)oak_f32_to_bits(f) << 32u) | OAK_TAG_F32;
  return value;
}

static inline struct oak_value_t oak_value_bool(const int b)
{
  struct oak_value_t value;
  value.bits = ((u64)(b ? 1u : 0u) << 32u) | OAK_TAG_BOOL;
  return value;
}

static inline struct oak_value_t oak_value_none(void)
{
  struct oak_value_t value;
  value.bits = OAK_TAG_NONE;
  return value;
}

static inline struct oak_value_t oak_value_handle(const u64 h)
{
  struct oak_value_t value;
  oak_assert((h >> 61u) == 0u); /* only 61 payload bits fit next to the tag */
  value.bits = (h << 3u) | OAK_TAG_HANDLE;
  return value;
}

static inline struct oak_value_t
oak_value_obj_encode(const struct oak_obj_t* obj,
                     const enum oak_value_tag_t tag)
{
  struct oak_value_t value;
  const u32 nonce = oak_obj_tables[obj->table_id].slots[obj->slot_index].nonce;
  value.bits = ((u64)nonce << OAK_OBJ_NONCE_SHIFT) |
               ((u64)obj->table_id << OAK_OBJ_TABLE_SHIFT) |
               ((u64)obj->slot_index << 3u) | tag;
  return value;
}

static inline struct oak_value_t oak_value_obj(struct oak_obj_t* obj)
{
  return oak_value_obj_encode(obj, OAK_TAG_OBJ);
}

static inline struct oak_value_t oak_value_weak_obj(struct oak_obj_t* obj)
{
  return oak_value_obj_encode(obj, OAK_TAG_WEAK);
}

static inline struct oak_value_t oak_value_native(void* payload)
{
  struct oak_value_t value;
  const u64 p = (u64)(usize)payload;
  oak_assert((p >> 61u) == 0u); /* only 61 payload bits fit next to the tag */
  value.bits = (p << 3u) | OAK_TAG_NATIVE;
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
  return oak_value_tag(value) == OAK_TAG_BOOL;
}

static inline int oak_is_number(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_I32 ||
         oak_value_tag(value) == OAK_TAG_F32;
}

static inline int oak_is_i32(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_I32;
}

static inline int oak_is_f32(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_F32;
}

static inline int oak_is_none(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_NONE;
}

static inline int oak_is_handle(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_HANDLE;
}

static inline u64 oak_value_as_handle(const struct oak_value_t value)
{
  oak_assert(oak_is_handle(value));
  return value.bits >> 3u;
}

static inline int oak_is_weak_obj(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_WEAK;
}

static inline int oak_is_native_value(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_NATIVE;
}

static inline void* oak_as_native_value(const struct oak_value_t value)
{
  oak_assert(oak_is_native_value(value));
  return (void*)(usize)(value.bits >> 3u);
}

static inline u32 oak_value_obj_index(const struct oak_value_t value)
{
  return (u32)(value.bits >> 3u) & OAK_OBJ_INDEX_MASK;
}

static inline u32 oak_value_obj_table(const struct oak_value_t value)
{
  return (u32)(value.bits >> OAK_OBJ_TABLE_SHIFT) & OAK_OBJ_TABLE_MASK;
}

static inline u32 oak_value_obj_nonce(const struct oak_value_t value)
{
  return (u32)(value.bits >> OAK_OBJ_NONCE_SHIFT);
}

/* Resolve an object value to its oak_obj_t without a nonce check.  Strong
 * references pin the object, so their slot (and table) cannot have been
 * recycled; for weak references the caller must have verified liveness
 * (oak_is_obj), or use oak_value_obj_resolve instead. */
static inline struct oak_obj_t* oak_val_obj_ptr(const struct oak_value_t value)
{
  return oak_obj_tables[oak_value_obj_table(value)]
      .slots[oak_value_obj_index(value)]
      .obj;
}

/* Nonce-checked resolution: null when the referenced object has died (the
 * slot's nonce moved on), even if the slot — or the whole table — has since
 * been reused.  The capacity check guards weak references into a recycled
 * table, whose slot array has been freed or reallocated smaller. */
static inline struct oak_obj_t*
oak_value_obj_resolve(const struct oak_value_t value)
{
  const struct oak_obj_table_t* table =
      &oak_obj_tables[oak_value_obj_table(value)];
  const u32 index = oak_value_obj_index(value);
  if (index >= table->capacity)
    return null;
  const struct oak_obj_slot_t* slot = &table->slots[index];
  return slot->nonce == oak_value_obj_nonce(value) ? slot->obj : null;
}

static inline int oak_is_obj(const struct oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    return 1;
  if (oak_value_tag(value) == OAK_TAG_WEAK)
    return oak_value_obj_resolve(value) != null;
  return 0;
}

static inline int oak_is_expired_weak(const struct oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_WEAK &&
         oak_value_obj_resolve(value) == null;
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
  return (int)(value.bits >> 32u);
}

static inline int oak_as_i32(const struct oak_value_t value)
{
  oak_assert(oak_is_i32(value));
  return (int)(i32)(u32)(value.bits >> 32u);
}

static inline float oak_as_f32(const struct oak_value_t value)
{
  oak_assert(oak_is_f32(value));
  return oak_f32_from_bits((u32)(value.bits >> 32u));
}

static inline struct oak_obj_t* oak_as_obj(const struct oak_value_t value)
{
  oak_assert(oak_is_obj(value));
  return oak_val_obj_ptr(value);
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
  /* Forwarded to the callback through oak_native_ctx_t::user_data. */
  void* user_data;
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
  /* Tombstone slot count; rebuilds must account for it or churn can leave
   * the table without a single EMPTY slot and probes would never end. */
  usize ht_tombstones;
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
  /* Per-binding user pointer from oak_bind_global_fn_t / oak_bind_fn_t;
   * null for builtins and bindings that did not set one. */
  void* user_data;
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

static inline void oak_value_incref(const struct oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    oak_obj_incref(oak_val_obj_ptr(value));
}

static inline void oak_value_decref(const struct oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    oak_obj_decref(oak_val_obj_ptr(value));
}

/* Weak references share the strong reference's index + nonce, so weakening
 * is a tag swap; no refcount is touched (weaks are trivially copyable). */
static inline struct oak_value_t
oak_value_weaken(const struct oak_value_t value)
{
  if (oak_is_obj(value))
  {
    struct oak_value_t weak;
    weak.bits = (value.bits & ~OAK_VALUE_TAG_MASK) | OAK_TAG_WEAK;
    return weak;
  }
  return value;
}

/* ===== Public API ===== */

OAK_API int oak_is_truthy(struct oak_value_t value);
OAK_API int oak_value_equal(struct oak_value_t a, struct oak_value_t b);

OAK_API struct oak_obj_string_t* oak_string_new(struct oak_allocator_t* a,
                                                const char* chars);

struct oak_obj_string_t* oak_string_new_len(struct oak_allocator_t* a,
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
                  const char* name,
                  void* user_data);

OAK_API struct oak_obj_array_t* oak_array_new(struct oak_allocator_t* a);
OAK_API void oak_array_push(struct oak_obj_array_t* arr,
                            struct oak_value_t value);

/* `type_name` and `field_names` are copied into the record; the caller's
 * strings only need to live for the duration of the call. */
OAK_API struct oak_obj_record_t* oak_record_new(
    struct oak_allocator_t* a,
    int field_count,
    const char* type_name,
    const char* const* field_names);

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
