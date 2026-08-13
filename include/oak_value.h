#pragma once

#include "oak_export.h"
#include "oak_refcount.h"
#include "oak_types.h"

/* Assertions in the inline accessors below follow the consumer's own NDEBUG
 * setting, exactly like <assert.h>.  No Oak build define affects this header:
 * an embedder gets the same layout and behaviour regardless of how the
 * library itself was configured.
 *
 * C++: this header compiles under a C++ compiler and every public header
 * carries an extern "C" block, but the string and record objects end in a
 * flexible array member, which is a C feature that C++ accepts only as an
 * extension (MSVC warns C4200).  Those two types must therefore be used
 * through pointers from the runtime, never value-copied or default-constructed
 * from C++. */
#include <assert.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Packed 8-byte value representation.
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
 *   OBJ/WEAK:      slot index in [31:3], table id in [37:32],
 *                  [39:38] reserved, nonce in [63:40]
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
typedef enum oak_value_tag oak_value_tag_t;
enum oak_value_tag
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

typedef enum oak_value_type oak_value_type_t;
enum oak_value_type
{
  OAK_VAL_BOOL,
  OAK_VAL_NUMBER,
  OAK_VAL_OBJ,
  OAK_VAL_WEAK_OBJ,
  OAK_VAL_NONE,
};

typedef enum oak_obj_type oak_obj_type_t;
enum oak_obj_type
{
  OAK_OBJ_STRING,
  OAK_OBJ_ARRAY,
  OAK_OBJ_MAP,
  OAK_OBJ_FN,
  OAK_OBJ_NATIVE_FN,
  OAK_OBJ_RECORD,
  OAK_OBJ_NATIVE_RECORD,
  OAK_OBJ_INTERFACE_OBJECT,
};

typedef struct oak_allocator oak_allocator_t;

typedef struct oak_obj oak_obj_t;
struct oak_obj
{
  oak_obj_type_t type;
  oak_refcount_t refcount;
  /* This object's slot in its object table (oak_obj_tables[table_id]). */
  u32 slot_index;
  u32 table_id;
  oak_allocator_t* allocator;
};

/*
 * Object tables.
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
 * acquires its own table in oak_vm_init and detaches it in oak_vm_free.  VM
 * allocation entry points pass that table explicitly, so a VM can move
 * between threads and one thread can operate on different VMs without any
 * implicit thread-local ownership state.  A detached table is recycled — its
 * slot array freed and its registry entry reusable — once the last object in
 * it dies; fresh slots then start at a nonce floor above every nonce the
 * previous incarnation issued, so stale weak references cannot alias across
 * recycles.
 *
 * Registry mutation is not synchronized.  Initialize VMs before starting a
 * parallel region, keep each active table confined to its VM thread, and free
 * VMs after joining the workers.  Shared table-0 objects must remain
 * read-only while the workers run. */

typedef struct oak_obj_slot oak_obj_slot_t;
struct oak_obj_slot
{
  oak_obj_t* obj; /* null while the slot is free */
  u32 nonce;             /* wraps at 24 bits; bumped when the object dies */
  u32 next_free;         /* freelist link, meaningful only while free */
};

typedef enum oak_obj_table_state oak_obj_table_state_t;
enum oak_obj_table_state
{
  OAK_OBJ_TABLE_FREE,
  OAK_OBJ_TABLE_ACTIVE,   /* owned by a VM (or table 0, owned by the process) */
  OAK_OBJ_TABLE_DETACHED, /* owner gone; recycled once live_count reaches 0 */
};

typedef struct oak_obj_table oak_obj_table_t;
struct oak_obj_table
{
  oak_obj_slot_t* slots;
  u32 capacity;
  u32 free_head;   /* OAK_OBJ_SLOT_NONE when the freelist is empty */
  u32 live_count;  /* occupied slots; gates recycling of detached tables */
  u32 nonce_floor; /* starting nonce for slots of the next incarnation */
  u8 state;        /* oak_obj_table_state_t */
};

#define OAK_OBJ_SLOT_NONE   0xFFFFFFFFu
#define OAK_OBJ_TABLE_COUNT 64u

OAK_API extern oak_obj_table_t oak_obj_tables[/*OAK_OBJ_TABLE_COUNT*/];

/* Reserve a table for a new owner (a VM).  Table 0 is reserved for shared
 * process-owned objects and is never returned.  Exhausting the registry is
 * fatal: silently sharing table 0 would break VM ownership isolation. */
OAK_API u32 oak_obj_table_acquire(void);

/* Declare the owner gone.  The table is recycled as soon as no object in it
 * remains alive.  No-op for table 0. */
OAK_API void oak_obj_table_detach(u32 table_id);

typedef struct oak_value oak_value_t;
struct oak_value
{
  /* Packed tag + payload — see the layout comment at the top of the file.
   * Access only through the constructors/predicates/extractors below. */
  u64 bits;
};

#define OAK_VALUE_TAG_MASK  ((u64)0x7u)
#define OAK_OBJ_INDEX_MASK  0x1FFFFFFFu /* 29 bits */
#define OAK_OBJ_TABLE_SHIFT 32u
#define OAK_OBJ_TABLE_MASK  0x3Fu /* 6 bits; bits 38..39 are reserved */
#define OAK_OBJ_NONCE_SHIFT 40u
#define OAK_OBJ_NONCE_MASK  0xFFFFFFu /* 24 bits */

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

static inline oak_value_tag_t oak_value_tag(const oak_value_t value)
{
  return (oak_value_tag_t)(value.bits & OAK_VALUE_TAG_MASK);
}

static inline oak_value_t oak_value_i32(const i32 i)
{
  oak_value_t value;
  value.bits = ((u64)(u32)i << 32u) | OAK_TAG_I32;
  return value;
}

static inline oak_value_t oak_value_f32(const float f)
{
  oak_value_t value;
  value.bits = ((u64)oak_f32_to_bits(f) << 32u) | OAK_TAG_F32;
  return value;
}

static inline oak_value_t oak_value_bool(const int b)
{
  oak_value_t value;
  value.bits = ((u64)(b ? 1u : 0u) << 32u) | OAK_TAG_BOOL;
  return value;
}

static inline oak_value_t oak_value_none(void)
{
  oak_value_t value;
  value.bits = OAK_TAG_NONE;
  return value;
}

static inline oak_value_t oak_value_handle(const u64 h)
{
  oak_value_t value;
  assert((h >> 61u) == 0u); /* only 61 payload bits fit next to the tag */
  value.bits = (h << 3u) | OAK_TAG_HANDLE;
  return value;
}

static inline oak_value_t
oak_value_obj_encode(const oak_obj_t* obj,
                     const oak_value_tag_t tag)
{
  oak_value_t value;
  const u32 nonce = oak_obj_tables[obj->table_id].slots[obj->slot_index].nonce;
  value.bits = ((u64)nonce << OAK_OBJ_NONCE_SHIFT) |
               ((u64)obj->table_id << OAK_OBJ_TABLE_SHIFT) |
               ((u64)obj->slot_index << 3u) | tag;
  return value;
}

static inline oak_value_t oak_value_obj(oak_obj_t* obj)
{
  return oak_value_obj_encode(obj, OAK_TAG_OBJ);
}

static inline oak_value_t oak_value_weak_obj(oak_obj_t* obj)
{
  return oak_value_obj_encode(obj, OAK_TAG_WEAK);
}

static inline oak_value_t oak_value_native(void* payload)
{
  oak_value_t value;
  const u64 p = (u64)(usize)payload;
  assert((p >> 61u) == 0u); /* only 61 payload bits fit next to the tag */
  value.bits = (p << 3u) | OAK_TAG_NATIVE;
  return value;
}

#define OAK_VALUE_I32(_i)        oak_value_i32(_i)
#define OAK_VALUE_F32(_f)        oak_value_f32(_f)
#define OAK_VALUE_BOOL(_b)       oak_value_bool(_b)
#define OAK_VALUE_NONE           oak_value_none()
#define OAK_VALUE_OBJ(_obj)      oak_value_obj((oak_obj_t*)(_obj))
#define OAK_VALUE_WEAK_OBJ(_obj) oak_value_weak_obj((oak_obj_t*)(_obj))
#define OAK_VALUE_NATIVE(_p)     oak_value_native((void*)(_p))

static inline int oak_is_bool(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_BOOL;
}

static inline int oak_is_number(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_I32 ||
         oak_value_tag(value) == OAK_TAG_F32;
}

static inline int oak_is_i32(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_I32;
}

static inline int oak_is_f32(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_F32;
}

static inline int oak_is_none(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_NONE;
}

static inline int oak_is_handle(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_HANDLE;
}

static inline u64 oak_value_as_handle(const oak_value_t value)
{
  assert(oak_is_handle(value));
  return value.bits >> 3u;
}

static inline int oak_is_weak_obj(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_WEAK;
}

static inline int oak_is_native_value(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_NATIVE;
}

static inline void* oak_as_native_value(const oak_value_t value)
{
  assert(oak_is_native_value(value));
  return (void*)(usize)(value.bits >> 3u);
}

static inline u32 oak_value_obj_index(const oak_value_t value)
{
  return (u32)(value.bits >> 3u) & OAK_OBJ_INDEX_MASK;
}

static inline u32 oak_value_obj_table(const oak_value_t value)
{
  return (u32)(value.bits >> OAK_OBJ_TABLE_SHIFT) & OAK_OBJ_TABLE_MASK;
}

static inline u32 oak_value_obj_nonce(const oak_value_t value)
{
  return (u32)(value.bits >> OAK_OBJ_NONCE_SHIFT);
}

/* Copying a VM-owned object reference into another VM would make the value
 * reachable through the wrong owner.  This applies to strong and weak
 * references: weak references are non-owning, but resolving one across VM
 * threads would still race the source table.  Table 0 is process-owned and
 * may be copied into any VM. */
static inline int oak_value_can_refcopy_to_table(const oak_value_t value,
                                                 const u32 dest_table)
{
  const oak_value_tag_t tag = oak_value_tag(value);
  if (tag != OAK_TAG_OBJ && tag != OAK_TAG_WEAK)
    return 1;
  const u32 src_table = oak_value_obj_table(value);
  return src_table == 0u || src_table == dest_table;
}

static inline void
oak_value_assert_can_refcopy_to_table(const oak_value_t value,
                                      const u32 dest_table)
{
  assert(oak_value_can_refcopy_to_table(value, dest_table));
}

/* Resolve an object value to its oak_obj_t without a nonce check.  Strong
 * references pin the object, so their slot (and table) cannot have been
 * recycled; for weak references the caller must have verified liveness
 * (oak_is_obj), or use oak_value_obj_resolve instead. */
static inline oak_obj_t* oak_val_obj_ptr(const oak_value_t value)
{
  return oak_obj_tables[oak_value_obj_table(value)]
      .slots[oak_value_obj_index(value)]
      .obj;
}

/* Nonce-checked resolution: null when the referenced object has died (the
 * slot's nonce moved on), even if the slot — or the whole table — has since
 * been reused.  The capacity check guards weak references into a recycled
 * table, whose slot array has been freed or reallocated smaller. */
static inline oak_obj_t*
oak_value_obj_resolve(const oak_value_t value)
{
  const oak_obj_table_t* table =
      &oak_obj_tables[oak_value_obj_table(value)];
  const u32 index = oak_value_obj_index(value);
  if (index >= table->capacity)
    return null;
  const oak_obj_slot_t* slot = &table->slots[index];
  return slot->nonce == oak_value_obj_nonce(value) ? slot->obj : null;
}

static inline int oak_is_obj(const oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    return 1;
  if (oak_value_tag(value) == OAK_TAG_WEAK)
    return oak_value_obj_resolve(value) != null;
  return 0;
}

static inline int oak_is_expired_weak(const oak_value_t value)
{
  return oak_value_tag(value) == OAK_TAG_WEAK &&
         oak_value_obj_resolve(value) == null;
}

static inline int oak_is_none_like(const oak_value_t value)
{
  return oak_is_none(value) || oak_is_expired_weak(value);
}

static inline int oak_is_string(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_STRING;
}

static inline int oak_is_fn(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_FN;
}

static inline int oak_is_native_fn(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_NATIVE_FN;
}

static inline int oak_is_array(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_ARRAY;
}

static inline int oak_is_map(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_MAP;
}

static inline int oak_is_record(const oak_value_t value)
{
  return oak_is_obj(value) && oak_val_obj_ptr(value)->type == OAK_OBJ_RECORD;
}

static inline int oak_is_native_record(const oak_value_t value)
{
  return oak_is_obj(value) &&
         oak_val_obj_ptr(value)->type == OAK_OBJ_NATIVE_RECORD;
}

static inline int oak_is_interface_object(const oak_value_t value)
{
  return oak_is_obj(value) &&
         oak_val_obj_ptr(value)->type == OAK_OBJ_INTERFACE_OBJECT;
}

static inline int oak_as_bool(const oak_value_t value)
{
  assert(oak_is_bool(value));
  return (int)(value.bits >> 32u);
}

static inline int oak_as_i32(const oak_value_t value)
{
  assert(oak_is_i32(value));
  return (int)(i32)(u32)(value.bits >> 32u);
}

static inline float oak_as_f32(const oak_value_t value)
{
  assert(oak_is_f32(value));
  return oak_f32_from_bits((u32)(value.bits >> 32u));
}

static inline oak_obj_t* oak_as_obj(const oak_value_t value)
{
  assert(oak_is_obj(value));
  return oak_val_obj_ptr(value);
}

typedef struct oak_attr_hook_entry oak_attr_hook_entry_t;

typedef struct oak_obj_string oak_obj_string_t;
struct oak_obj_string
{
  oak_obj_t obj;
  usize length;
  u32 hash;
  char chars[];
};

typedef struct oak_obj_fn oak_obj_fn_t;
struct oak_obj_fn
{
  oak_obj_t obj;
  usize code_offset;
  int arity;
  u16 module_id;
  const char* name;
  oak_attr_hook_entry_t* attr_hooks;
  int attr_hook_count;
};

typedef struct oak_native_call oak_native_call_t;
/* Declared here and defined in oak_bind.h: a native function object remembers
 * the type it was bound as a method on, and hands it to the callback. */
typedef struct oak_bind_type oak_bind_type_t;

typedef enum oak_fn_call_result oak_fn_call_result_t;
enum oak_fn_call_result
{
  OAK_FN_CALL_OK = 0,
  OAK_FN_CALL_RUNTIME_ERROR,
};

typedef oak_fn_call_result_t (*oak_attr_runtime_cb_t)(
    oak_native_call_t* call,
    const char* fn_name,
    const oak_value_t* args,
    int argc,
    void* user_data);

struct oak_attr_hook_entry
{
  oak_attr_runtime_cb_t cb;
  void* ud;
};

typedef oak_fn_call_result_t (*oak_native_fn_t)(
    oak_native_call_t* call,
    const oak_value_t* args,
    int argc,
    oak_value_t* out_result);

typedef struct oak_obj_native_fn oak_obj_native_fn_t;
struct oak_obj_native_fn
{
  oak_obj_t obj;
  oak_native_fn_t fn;
  int arity;
  const char* name;
  /* Forwarded to the callback through oak_native_call_t::user_data. */
  void* user_data;
  /* Receiver type when this is a method bound with oak_bind_fn; null for
   * global functions and builtins.  Forwarded as oak_native_call_t::self_type.
   * Set after construction by whoever interns the method, so oak_native_fn_new
   * keeps its signature. */
  const oak_bind_type_t* self_type;
  oak_attr_hook_entry_t* attr_hooks;
  int attr_hook_count;
};

typedef struct oak_obj_array oak_obj_array_t;
struct oak_obj_array
{
  oak_obj_t obj;
  usize length;
  usize capacity;
  oak_value_t* items;
};

typedef struct oak_map_entry oak_map_entry_t;
struct oak_map_entry
{
  oak_value_t key;
  oak_value_t value;
};

#define MAP_HT_EMPTY     ((usize)-1)
#define MAP_HT_TOMBSTONE ((usize)-2)

typedef struct oak_obj_map oak_obj_map_t;
struct oak_obj_map
{
  oak_obj_t obj;
  usize length;
  usize capacity;
  oak_map_entry_t* entries;
  usize ht_capacity;
  usize* ht;
  /* Tombstone slot count; rebuilds must account for it or churn can leave
   * the table without a single EMPTY slot and probes would never end. */
  usize ht_tombstones;
};

typedef struct oak_obj_record oak_obj_record_t;
struct oak_obj_record
{
  oak_obj_t obj;
  const char* type_name;
  int field_count;
  const char* const* field_name_ptrs;
  void* field_name_storage;
  oak_value_t fields[];
};

typedef struct oak_obj_native_record oak_obj_native_record_t;
struct oak_obj_native_record
{
  oak_obj_t obj;
  void* instance;
  const oak_bind_type_t* type;
};

typedef struct oak_obj_interface_object oak_obj_interface_object_t;
struct oak_obj_interface_object
{
  oak_obj_t obj;
  oak_value_t value;
  oak_obj_array_t* vtable;
};

typedef struct oak_vm oak_vm_t;

struct oak_native_call
{
  oak_vm_t* vm;
  oak_allocator_t* allocator;
  /* Per-binding user pointer from oak_bind_global_fn_t / oak_bind_fn_t;
   * null for builtins and bindings that did not set one. */
  void* user_data;
  /* Name the binding was registered under; null for anonymous natives.
   * Error messages raised with oak_native_error are prefixed with it, so a
   * callback rarely needs to read it directly. */
  const char* fn_name;
  /* The receiver type, for a method registered with oak_bind_fn; null for
   * global functions and builtins.  Reach it through oak_arg_self and
   * oak_native_self_new rather than reading it: a binding no longer has to
   * pass its own descriptor through user_data just to unwrap its receiver or
   * construct another instance of itself. */
  const oak_bind_type_t* self_type;
};

static inline oak_obj_string_t*
oak_as_string(const oak_value_t value)
{
  assert(oak_is_string(value));
  return (oak_obj_string_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_fn_t* oak_as_fn(const oak_value_t value)
{
  assert(oak_is_fn(value));
  return (oak_obj_fn_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_native_fn_t*
oak_as_native_fn(const oak_value_t value)
{
  assert(oak_is_native_fn(value));
  return (oak_obj_native_fn_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_array_t*
oak_as_array(const oak_value_t value)
{
  assert(oak_is_array(value));
  return (oak_obj_array_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_map_t* oak_as_map(const oak_value_t value)
{
  assert(oak_is_map(value));
  return (oak_obj_map_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_record_t*
oak_as_record(const oak_value_t value)
{
  assert(oak_is_record(value));
  return (oak_obj_record_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_native_record_t*
oak_as_native_record(const oak_value_t value)
{
  assert(oak_is_native_record(value));
  return (oak_obj_native_record_t*)oak_val_obj_ptr(value);
}

static inline oak_obj_interface_object_t*
oak_as_interface_object(const oak_value_t value)
{
  assert(oak_is_interface_object(value));
  return (oak_obj_interface_object_t*)oak_val_obj_ptr(value);
}

static inline char* oak_as_cstring(const oak_value_t value)
{
  return oak_as_string(value)->chars;
}

OAK_API void oak_obj_incref(oak_obj_t* obj);
OAK_API void oak_obj_decref(oak_obj_t* obj);

static inline void oak_value_incref(const oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    oak_obj_incref(oak_val_obj_ptr(value));
}

static inline void oak_value_decref(const oak_value_t value)
{
  if (oak_value_tag(value) == OAK_TAG_OBJ)
    oak_obj_decref(oak_val_obj_ptr(value));
}

/* Weak references share the strong reference's index + nonce, so weakening
 * is a tag swap; no refcount is touched (weaks are trivially copyable). */
static inline oak_value_t
oak_value_weaken(const oak_value_t value)
{
  if (oak_is_obj(value))
  {
    oak_value_t weak;
    weak.bits = (value.bits & ~OAK_VALUE_TAG_MASK) | OAK_TAG_WEAK;
    return weak;
  }
  return value;
}

OAK_API int oak_is_truthy(oak_value_t value);
OAK_API int oak_value_equal(oak_value_t a, oak_value_t b);

/* The oak_*_new constructors below create process-shared table-0 objects.  To
 * create an object owned by a particular VM, use the oak_vm_*_new functions in
 * oak_vm.h instead. */
OAK_API oak_obj_string_t* oak_string_new(oak_allocator_t* a,
                                         const char* chars);

OAK_API oak_obj_string_t* oak_string_concat(oak_allocator_t* a,
                                            const oak_obj_string_t* s1,
                                            const oak_obj_string_t* s2);

OAK_API oak_obj_fn_t* oak_fn_new(oak_allocator_t* a,
                                 usize code_offset,
                                 int arity,
                                 u16 module_id);

OAK_API oak_obj_native_fn_t* oak_native_fn_new(oak_allocator_t* a,
                                               oak_native_fn_t fn,
                                               int arity,
                                               const char* name,
                                               void* user_data);

OAK_API oak_obj_array_t* oak_array_new(oak_allocator_t* a);
/* Return 0 without modifying the array when `value` belongs to another VM. */
OAK_API int oak_array_push(oak_obj_array_t* arr,
                           oak_value_t value);

/* `type_name` and `field_names` are copied into the record; the caller's
 * strings only need to live for the duration of the call. */
OAK_API oak_obj_record_t* oak_record_new(oak_allocator_t* a,
                                         int field_count,
                                         const char* type_name,
                                         const char* const* field_names);

OAK_API oak_obj_native_record_t*
oak_obj_native_record_new(oak_allocator_t* a,
                          const oak_bind_type_t* type,
                          void* instance);

OAK_API oak_obj_interface_object_t*
oak_interface_object_new(oak_allocator_t* a,
                         oak_value_t value,
                         oak_obj_array_t* vtable);

OAK_API oak_obj_map_t* oak_map_new(oak_allocator_t* a);
OAK_API int oak_map_get(const oak_obj_map_t* map,
                        oak_value_t key,
                        oak_value_t* out);
/* Return 0 without modifying the map for an invalid key or when either value
 * belongs to another VM. */
OAK_API int oak_map_set(oak_obj_map_t* map,
                        oak_value_t key,
                        oak_value_t value);
OAK_API int oak_map_has(const oak_obj_map_t* map,
                        oak_value_t key);
OAK_API int oak_map_delete(oak_obj_map_t* map, oak_value_t key);
OAK_API oak_value_t oak_map_key_at(const oak_obj_map_t* map, usize index);
OAK_API oak_value_t oak_map_value_at(const oak_obj_map_t* map, usize index);

OAK_API int oak_native_fn_format(char* buf,
                                 usize size,
                                 const oak_obj_native_fn_t* native);

OAK_API oak_obj_string_t* oak_value_to_string(oak_allocator_t* allocator,
                                              oak_value_t value);

OAK_API void oak_value_println(oak_allocator_t* allocator,
                               oak_value_t value);

OAK_API int
oak_value_snprint_repr(char* buf, usize size, oak_value_t value);

OAK_API oak_obj_string_t*
oak_string_from_value_repr(oak_allocator_t* allocator, oak_value_t value);

#ifdef __cplusplus
}
#endif
