#include "oak_value.h"

#include "oak_allocator.h"
#include "oak_bind.h"
#include "oak_log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The object-table registry (see oak_value.h).  Slot arrays are allocated
 * with plain realloc rather than an oak_allocator_t: tables outlive every
 * allocator (including leak-tracking ones, which would otherwise report
 * them).  Zero-initialization leaves every entry FREE with no slots; the
 * shared table 0 is special-cased below and never handed out or recycled. */
struct oak_obj_table_t oak_obj_tables[OAK_OBJ_TABLE_COUNT];

/* Objects created on this thread land in this table.  Defaults to the
 * shared table 0; the VM run entry points scope it to the running VM. */
static _Thread_local u32 oak_current_obj_table = 0u;

u32 oak_obj_table_set_current(const u32 table_id)
{
  oak_assert(table_id < OAK_OBJ_TABLE_COUNT);
  const u32 prev = oak_current_obj_table;
  oak_current_obj_table = table_id;
  return prev;
}

u32 oak_obj_table_acquire(void)
{
  for (u32 id = 1u; id < OAK_OBJ_TABLE_COUNT; ++id)
  {
    if (oak_obj_tables[id].state == OAK_OBJ_TABLE_FREE)
    {
      oak_obj_tables[id].state = OAK_OBJ_TABLE_ACTIVE;
      return id;
    }
  }
  /* Every entry is taken: fall back to the shared table, which is never
   * recycled.  Isolation degrades but correctness does not. */
  oak_log(OAK_LOG_WARN, "oak: object-table registry exhausted; sharing table 0");
  return 0u;
}

static void oak_obj_table_try_recycle(struct oak_obj_table_t* table)
{
  if (table->state != OAK_OBJ_TABLE_DETACHED || table->live_count != 0u)
    return;

  /* Raise the nonce floor above everything this incarnation issued so a
   * stale weak reference can never match a slot of the next incarnation. */
  u32 max_nonce = table->nonce_floor;
  for (u32 i = 0; i < table->capacity; ++i)
  {
    if (table->slots[i].nonce > max_nonce)
      max_nonce = table->slots[i].nonce;
  }
  table->nonce_floor = (max_nonce + 1u) & OAK_OBJ_NONCE_MASK;

  free(table->slots);
  table->slots = null;
  table->capacity = 0u;
  table->free_head = OAK_OBJ_SLOT_NONE;
  table->state = OAK_OBJ_TABLE_FREE;
}

void oak_obj_table_detach(const u32 table_id)
{
  if (table_id == 0u || table_id >= OAK_OBJ_TABLE_COUNT)
    return;
  struct oak_obj_table_t* table = &oak_obj_tables[table_id];
  oak_assert(table->state == OAK_OBJ_TABLE_ACTIVE);
  table->state = OAK_OBJ_TABLE_DETACHED;
  oak_obj_table_try_recycle(table);
}

static u32 oak_obj_table_insert(const u32 table_id, struct oak_obj_t* obj)
{
  struct oak_obj_table_t* table = &oak_obj_tables[table_id];
  oak_assert(table_id == 0u || table->state == OAK_OBJ_TABLE_ACTIVE);

  /* A zero-initialized registry entry has free_head == 0 with no slots, so
   * an empty capacity must be normalized before the freelist check. */
  if (table->capacity == 0u)
    table->free_head = OAK_OBJ_SLOT_NONE;

  if (table->free_head == OAK_OBJ_SLOT_NONE)
  {
    const u32 old_cap = table->capacity;
    const u32 new_cap = old_cap == 0u ? 256u : old_cap * 2u;
    if (new_cap > OAK_OBJ_INDEX_MASK + 1u)
      oak_panic();
    struct oak_obj_slot_t* slots =
        realloc(table->slots, (usize)new_cap * sizeof(*slots));
    if (!slots)
      oak_panic();
    /* Push the fresh slots in reverse so the lowest index is handed out
     * first.  Nonces start at the table's floor and only move when an
     * object dies. */
    for (u32 i = new_cap; i-- > old_cap;)
    {
      slots[i].obj = null;
      slots[i].nonce = table->nonce_floor;
      slots[i].next_free = table->free_head;
      table->free_head = i;
    }
    table->slots = slots;
    table->capacity = new_cap;
  }

  const u32 index = table->free_head;
  struct oak_obj_slot_t* slot = &table->slots[index];
  table->free_head = slot->next_free;
  slot->obj = obj;
  table->live_count++;
  return index;
}

static void oak_obj_table_release(const struct oak_obj_t* obj)
{
  struct oak_obj_table_t* table = &oak_obj_tables[obj->table_id];
  struct oak_obj_slot_t* slot = &table->slots[obj->slot_index];
  slot->obj = null;
  /* Expire every outstanding weak reference to this slot's object. */
  slot->nonce = (slot->nonce + 1u) & OAK_OBJ_NONCE_MASK;
  slot->next_free = table->free_head;
  table->free_head = obj->slot_index;
  table->live_count--;
  oak_obj_table_try_recycle(table);
}

static void oak_obj_init(struct oak_obj_t* obj,
                         const enum oak_obj_type_t type,
                         struct oak_allocator_t* allocator)
{
  obj->type = type;
  oak_refcount_init(&obj->refcount, 1);
  obj->allocator = allocator;
  obj->table_id = oak_current_obj_table;
  obj->slot_index = oak_obj_table_insert(obj->table_id, obj);
}

void oak_obj_incref(struct oak_obj_t* obj)
{
  oak_refcount_inc(&obj->refcount);
}

/* Release the object's owned references and free any owned buffers, without
 * freeing the oak_obj_t header itself. */
static void oak_obj_destroy_payload(struct oak_obj_t* obj)
{
  struct oak_allocator_t* a = obj->allocator;

  if (obj->type == OAK_OBJ_ARRAY)
  {
    struct oak_obj_array_t* arr = (struct oak_obj_array_t*)obj;
    for (usize i = 0; i < arr->length; ++i)
      oak_value_decref(arr->items[i]);
    if (arr->items)
      OAK_FREE(a, arr->items);
  }
  else if (obj->type == OAK_OBJ_MAP)
  {
    struct oak_obj_map_t* map = (struct oak_obj_map_t*)obj;
    for (usize i = 0; i < map->length; ++i)
    {
      oak_value_decref(map->entries[i].key);
      oak_value_decref(map->entries[i].value);
    }
    if (map->entries)
      OAK_FREE(a, map->entries);
    if (map->ht)
      OAK_FREE(a, map->ht);
  }
  else if (obj->type == OAK_OBJ_RECORD)
  {
    struct oak_obj_record_t* s = (struct oak_obj_record_t*)obj;
    if (s->field_name_storage)
      OAK_FREE(a, s->field_name_storage);
    for (int i = 0; i < s->field_count; ++i)
      oak_value_decref(s->fields[i]);
  }
  else if (obj->type == OAK_OBJ_NATIVE_RECORD)
  {
    struct oak_obj_native_record_t* ns = (struct oak_obj_native_record_t*)obj;
    if (ns->instance && ns->type && ns->type->destructor)
      ns->type->destructor(ns->instance);
  }
  else if (obj->type == OAK_OBJ_INTERFACE_OBJECT)
  {
    struct oak_obj_interface_object_t* to = (struct oak_obj_interface_object_t*)obj;
    oak_value_decref(to->value);
    oak_obj_decref((struct oak_obj_t*)to->vtable);
  }
  else if (obj->type == OAK_OBJ_FN)
  {
    struct oak_obj_fn_t* fn = (struct oak_obj_fn_t*)obj;
    if (fn->name)
      OAK_FREE(a, (void*)fn->name);
    if (fn->attr_hooks)
      OAK_FREE(a, fn->attr_hooks);
  }
  else if (obj->type == OAK_OBJ_NATIVE_FN)
  {
    struct oak_obj_native_fn_t* nfn = (struct oak_obj_native_fn_t*)obj;
    if (nfn->attr_hooks)
      OAK_FREE(a, nfn->attr_hooks);
  }
}

void oak_obj_decref(struct oak_obj_t* obj)
{
  if (!oak_refcount_dec(&obj->refcount))
    return;

  /* Release the slot before running destructors so any weak reference
   * touched during teardown (including ones to this object) already reads
   * as expired. */
  oak_obj_table_release(obj);
  oak_obj_destroy_payload(obj);
  OAK_FREE(obj->allocator, obj);
}

static u32 hash_string(const char* chars, const usize length)
{
  u32 hash = 2166136261u;
  for (usize i = 0; i < length; ++i)
  {
    hash ^= (u8)chars[i];
    hash *= 16777619u;
  }
  return hash;
}

struct oak_obj_string_t* oak_string_new_len(struct oak_allocator_t* a,
                                            const char* chars,
                                            const usize length)
{
  struct oak_obj_string_t* str =
      OAK_ALLOC(a, sizeof(struct oak_obj_string_t) + length + 1);
  oak_obj_init(&str->obj, OAK_OBJ_STRING, a);
  str->length = length;
  memcpy(str->chars, chars, length);
  str->chars[length] = 0;
  str->hash = hash_string(chars, length);
  return str;
}

struct oak_obj_string_t* oak_string_new(struct oak_allocator_t* a,
                                        const char* chars)
{
  return oak_string_new_len(a, chars, strlen(chars));
}

struct oak_obj_fn_t* oak_fn_new(struct oak_allocator_t* a,
                                const usize code_offset,
                                const int arity,
                                const u16 module_id)
{
  struct oak_obj_fn_t* fn = OAK_ALLOC(a, sizeof(struct oak_obj_fn_t));
  oak_obj_init(&fn->obj, OAK_OBJ_FN, a);
  fn->code_offset = code_offset;
  fn->arity = arity;
  fn->module_id = module_id;
  fn->name = null;
  fn->attr_hooks = null;
  fn->attr_hook_count = 0;
  return fn;
}

struct oak_obj_native_fn_t*
oak_native_fn_new(struct oak_allocator_t* a,
                  const oak_native_fn_t fn,
                  const int arity,
                  const char* name,
                  void* user_data)
{
  struct oak_obj_native_fn_t* native =
      OAK_ALLOC(a, sizeof(struct oak_obj_native_fn_t));
  oak_obj_init(&native->obj, OAK_OBJ_NATIVE_FN, a);
  native->fn = fn;
  native->arity = arity;
  native->name = name;
  native->user_data = user_data;
  native->attr_hooks = null;
  native->attr_hook_count = 0;
  return native;
}

int oak_native_fn_format(char* buf,
                         const usize size,
                         const struct oak_obj_native_fn_t* native)
{
  const void* fn_ptr = (const void*)(uintptr_t)native->fn;
  if (native->name && native->name[0] != '\0')
    return snprintf(buf,
                    size,
                    "<native %s arity=%d fn=%p>",
                    native->name,
                    native->arity,
                    fn_ptr);
  return snprintf(buf, size, "<native arity=%d fn=%p>", native->arity, fn_ptr);
}

struct oak_obj_array_t* oak_array_new(struct oak_allocator_t* a)
{
  struct oak_obj_array_t* arr =
      OAK_ALLOC(a, sizeof(struct oak_obj_array_t));
  oak_obj_init(&arr->obj, OAK_OBJ_ARRAY, a);
  arr->length = 0;
  arr->capacity = 0;
  arr->items = null;
  return arr;
}

void oak_array_push(struct oak_obj_array_t* arr, const struct oak_value_t value)
{
  oak_value_assert_can_refcopy_to_table(value, arr->obj.table_id);
  if (arr->length >= arr->capacity)
  {
    const usize new_cap = arr->capacity == 0 ? 8u : arr->capacity * 2u;
    arr->items = OAK_REALLOC(arr->obj.allocator,
        arr->items, new_cap * sizeof(struct oak_value_t));
    arr->capacity = new_cap;
  }
  oak_value_incref(value);
  arr->items[arr->length++] = value;
}

struct oak_obj_record_t* oak_record_new(struct oak_allocator_t* a,
                                        const int field_count,
                                        const char* const type_name,
                                        const char* const* const field_names)
{
  oak_assert(field_count >= 0);
  const usize size = sizeof(struct oak_obj_record_t) +
                     (usize)field_count * sizeof(struct oak_value_t);
  struct oak_obj_record_t* s = OAK_ALLOC(a, size);
  oak_obj_init(&s->obj, OAK_OBJ_RECORD, a);
  s->type_name = null;
  s->field_count = field_count;
  s->field_name_ptrs = null;
  s->field_name_storage = null;
  for (int i = 0; i < field_count; ++i)
    s->fields[i] = OAK_VALUE_I32(0);

  /* Copy the type name and field names into one owned blob: records can
   * outlive their chunk, so borrowing the caller's pointers would dangle. */
  const int have_fields = field_names && field_count > 0;
  const usize ptrs_size =
      have_fields ? (usize)field_count * sizeof(const char*) : 0u;
  usize strings_total = 0u;
  if (have_fields)
  {
    for (int i = 0; i < field_count; ++i)
      strings_total += strlen(field_names[i]) + 1u;
  }
  const usize type_name_size = type_name ? strlen(type_name) + 1u : 0u;
  if (ptrs_size + strings_total + type_name_size > 0u)
  {
    char* const raw = OAK_ALLOC(a, ptrs_size + strings_total + type_name_size);
    s->field_name_storage = raw;
    char* p = raw + ptrs_size;
    if (have_fields)
    {
      const char** const ptrs = (const char**)raw;
      s->field_name_ptrs = (const char* const*)ptrs;
      for (int i = 0; i < field_count; ++i)
      {
        const usize n = strlen(field_names[i]);
        memcpy(p, field_names[i], n);
        p[n] = '\0';
        ptrs[i] = p;
        p += n + 1u;
      }
    }
    if (type_name)
    {
      memcpy(p, type_name, type_name_size);
      s->type_name = p;
    }
  }
  return s;
}

struct oak_obj_native_record_t*
oak_obj_native_record_new(struct oak_allocator_t* a,
                          const struct oak_bind_type_t* type,
                          void* instance)
{
  struct oak_obj_native_record_t* ns =
      OAK_ALLOC(a, sizeof(struct oak_obj_native_record_t));
  oak_obj_init(&ns->obj, OAK_OBJ_NATIVE_RECORD, a);
  ns->instance = instance;
  ns->type = type;
  return ns;
}

struct oak_obj_interface_object_t*
oak_interface_object_new(struct oak_allocator_t* a,
                     struct oak_value_t value,
                     struct oak_obj_array_t* vtable)
{
  struct oak_obj_interface_object_t* to =
      OAK_ALLOC(a, sizeof(struct oak_obj_interface_object_t));
  oak_obj_init(&to->obj, OAK_OBJ_INTERFACE_OBJECT, a);
  oak_value_assert_can_refcopy_to_table(value, to->obj.table_id);
  oak_assert(vtable->obj.table_id == 0u ||
             vtable->obj.table_id == to->obj.table_id);
  oak_value_incref(value);
  to->value = value;
  oak_obj_incref((struct oak_obj_t*)vtable);
  to->vtable = vtable;
  return to;
}

struct oak_obj_map_t* oak_map_new(struct oak_allocator_t* a)
{
  struct oak_obj_map_t* map =
      OAK_ALLOC(a, sizeof(struct oak_obj_map_t));
  oak_obj_init(&map->obj, OAK_OBJ_MAP, a);
  map->length = 0;
  map->capacity = 0;
  map->entries = null;
  map->ht_capacity = 0;
  map->ht = null;
  map->ht_tombstones = 0;
  return map;
}

static u32 hash_value(const struct oak_value_t v)
{
  if (oak_is_bool(v))
    return (u32)oak_as_bool(v) * 2654435761u;
  if (oak_is_i32(v))
    return (u32)oak_as_i32(v) * 2654435761u;
  if (oak_is_f32(v))
  {
    /* Values that compare equal must hash identically: integral floats in
     * i32 range hash as the equal integer (this also unifies +0.0/-0.0),
     * matching the numeric coercion in oak_value_equal. */
    const float f = oak_as_f32(v);
    if (f >= -2147483648.0f && f < 2147483648.0f && (float)(i32)f == f)
      return (u32)(i32)f * 2654435761u;
    return oak_f32_to_bits(f) * 2654435761u;
  }
  if (oak_is_none(v))
    return 0x9E3779B9u;
  if (oak_is_handle(v))
  {
    const u64 h = oak_value_as_handle(v);
    return (u32)(h ^ (h >> 32)) * 2654435761u;
  }
  if (oak_is_string(v))
    return oak_as_string(v)->hash;
  {
    struct oak_obj_t* p = oak_val_obj_ptr(v);
    return (u32)(uintptr_t)p * 2654435761u;
  }
}

/* Keys must be stable under oak_value_equal: none and weak refs are excluded,
 * and so is NaN because it never compares equal to itself — a NaN key could be
 * inserted but never looked up or deleted again. */
static int map_key_invalid(const struct oak_value_t key)
{
  if (oak_is_none(key) || oak_is_weak_obj(key))
    return 1;
  return oak_is_f32(key) && oak_as_f32(key) != oak_as_f32(key);
}

static usize ht_probe(const usize* ht,
                      const usize ht_cap,
                      const struct oak_map_entry_t* entries,
                      const struct oak_value_t key,
                      usize* out_idx)
{
  const u32 hash = hash_value(key);
  usize slot = (usize)(hash & (u32)(ht_cap - 1u));
  usize first_tomb = MAP_HT_EMPTY;
  for (;;)
  {
    const usize idx = ht[slot];
    if (idx == MAP_HT_EMPTY)
    {
      *out_idx = MAP_HT_EMPTY;
      return (first_tomb != MAP_HT_EMPTY) ? first_tomb : slot;
    }
    if (idx == MAP_HT_TOMBSTONE)
    {
      if (first_tomb == MAP_HT_EMPTY)
        first_tomb = slot;
    }
    else if (oak_value_equal(entries[idx].key, key))
    {
      *out_idx = idx;
      return slot;
    }
    slot = (slot + 1u) & (ht_cap - 1u);
  }
}

static void map_ht_rebuild(struct oak_obj_map_t* map, const usize new_cap)
{
  struct oak_allocator_t* a = map->obj.allocator;
  usize* new_ht = OAK_ALLOC(a, new_cap * sizeof(usize));
  for (usize i = 0; i < new_cap; ++i)
    new_ht[i] = MAP_HT_EMPTY;

  for (usize i = 0; i < map->length; ++i)
  {
    const u32 hash = hash_value(map->entries[i].key);
    usize slot = (usize)(hash & (u32)(new_cap - 1u));
    while (new_ht[slot] != MAP_HT_EMPTY)
      slot = (slot + 1u) & (new_cap - 1u);
    new_ht[slot] = i;
  }

  if (map->ht)
    OAK_FREE(a, map->ht);
  map->ht = new_ht;
  map->ht_capacity = new_cap;
  map->ht_tombstones = 0;
}

int oak_map_get(const struct oak_obj_map_t* map,
                const struct oak_value_t key,
                struct oak_value_t* out)
{
  if (map_key_invalid(key))
    return 0;
  if (!map->ht || map->length == 0)
    return 0;
  usize entry_idx;
  ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  if (entry_idx == MAP_HT_EMPTY)
    return 0;
  if (out)
    *out = map->entries[entry_idx].value;
  return 1;
}

int oak_map_has(const struct oak_obj_map_t* map, const struct oak_value_t key)
{
  if (map_key_invalid(key))
    return 0;
  if (!map->ht || map->length == 0)
    return 0;
  usize entry_idx;
  ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  return entry_idx != MAP_HT_EMPTY;
}

int oak_map_delete(struct oak_obj_map_t* map, const struct oak_value_t key)
{
  if (map_key_invalid(key))
    return 0;
  if (!map->ht || map->length == 0)
    return 0;

  usize entry_idx;
  const usize del_slot =
      ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);
  if (entry_idx == MAP_HT_EMPTY)
    return 0;

  const struct oak_value_t del_key = map->entries[entry_idx].key;
  const struct oak_value_t del_val = map->entries[entry_idx].value;

  map->ht[del_slot] = MAP_HT_TOMBSTONE;
  map->ht_tombstones++;

  const usize last = map->length - 1u;
  if (entry_idx != last)
  {
    map->entries[entry_idx] = map->entries[last];

    usize moved_idx;
    const usize moved_slot = ht_probe(map->ht,
                                      map->ht_capacity,
                                      map->entries,
                                      map->entries[entry_idx].key,
                                      &moved_idx);
    (void)moved_idx;
    map->ht[moved_slot] = entry_idx;
  }

  map->length--;
  oak_value_decref(del_key);
  oak_value_decref(del_val);
  return 1;
}

struct oak_value_t oak_map_key_at(const struct oak_obj_map_t* map,
                                  const usize index)
{
  oak_assert(index < map->length);
  return map->entries[index].key;
}

struct oak_value_t oak_map_value_at(const struct oak_obj_map_t* map,
                                    const usize index)
{
  oak_assert(index < map->length);
  return map->entries[index].value;
}

int oak_map_set(struct oak_obj_map_t* map,
                const struct oak_value_t key,
                const struct oak_value_t value)
{
  if (map_key_invalid(key))
    return 0;
  /* Tombstones count toward the load factor so insert/delete churn cannot
   * exhaust the EMPTY slots that terminate probing; capacity itself only
   * grows when live entries demand it. */
  if (!map->ht ||
      (map->length + map->ht_tombstones + 1u) * 4u > map->ht_capacity * 3u)
  {
    usize new_cap = map->ht_capacity < 8u ? 8u : map->ht_capacity;
    while ((map->length + 1u) * 4u > new_cap * 3u)
      new_cap *= 2u;
    map_ht_rebuild(map, new_cap);
  }

  usize entry_idx;
  const usize slot =
      ht_probe(map->ht, map->ht_capacity, map->entries, key, &entry_idx);

  if (entry_idx != MAP_HT_EMPTY)
  {
    oak_value_assert_can_refcopy_to_table(value, map->obj.table_id);
    oak_value_incref(value);
    oak_value_decref(map->entries[entry_idx].value);
    map->entries[entry_idx].value = value;
    return 1;
  }

  if (map->length >= map->capacity)
  {
    const usize new_cap = map->capacity == 0u ? 8u : map->capacity * 2u;
    map->entries = OAK_REALLOC(map->obj.allocator,
        map->entries, new_cap * sizeof(struct oak_map_entry_t));
    map->capacity = new_cap;
  }

  oak_value_assert_can_refcopy_to_table(key, map->obj.table_id);
  oak_value_assert_can_refcopy_to_table(value, map->obj.table_id);
  oak_value_incref(key);
  oak_value_incref(value);
  map->entries[map->length].key = key;
  map->entries[map->length].value = value;
  if (map->ht[slot] == MAP_HT_TOMBSTONE)
    map->ht_tombstones--;
  map->ht[slot] = map->length;
  map->length++;
  return 1;
}

struct oak_obj_string_t* oak_string_concat(struct oak_allocator_t* al,
                                           const struct oak_obj_string_t* a,
                                           const struct oak_obj_string_t* b)
{
  const usize length = a->length + b->length;
  struct oak_obj_string_t* str =
      OAK_ALLOC(al, sizeof(struct oak_obj_string_t) + length + 1);
  oak_obj_init(&str->obj, OAK_OBJ_STRING, al);
  str->length = length;
  memcpy(str->chars, a->chars, a->length);
  memcpy(str->chars + a->length, b->chars, b->length);
  str->chars[length] = 0;
  str->hash = hash_string(str->chars, length);
  return str;
}

int oak_is_truthy(const struct oak_value_t value)
{
  if (oak_is_none_like(value))
    return 0;
  if (oak_is_bool(value))
    return oak_as_bool(value);
  if (oak_is_number(value))
  {
    if (oak_is_f32(value))
      return oak_as_f32(value) != 0.0f;
    return oak_as_i32(value) != 0;
  }
  if (oak_is_obj(value))
    return 1;
  /* Inline value types are first-class values; like heap objects/records they
   * are always truthy (their opaque payload has no falsy interpretation). */
  if (oak_is_native_value(value))
    return 1;
  return 0;
}

int oak_value_equal(const struct oak_value_t a, const struct oak_value_t b)
{
  const int a_none = oak_is_none_like(a);
  const int b_none = oak_is_none_like(b);
  if (a_none || b_none)
    return a_none && b_none;

  if (oak_is_obj(a) && oak_is_obj(b))
  {
    if (oak_is_string(a) && oak_is_string(b))
    {
      const struct oak_obj_string_t* str_a = oak_as_string(a);
      const struct oak_obj_string_t* str_b = oak_as_string(b);
      if (str_a->hash != str_b->hash)
        return 0;
      return strcmp(str_a->chars, str_b->chars) == 0;
    }
    return oak_as_obj(a) == oak_as_obj(b);
  }

  /* Numbers compare by value across the i32/f32 divide, matching the
   * ordering operators (< <= > >=). Both types convert to double exactly,
   * so the mixed comparison is precise even beyond f32's 24-bit mantissa. */
  if (oak_is_number(a) && oak_is_number(b) &&
      oak_value_tag(a) != oak_value_tag(b))
  {
    const double da = oak_is_i32(a) ? (double)oak_as_i32(a)
                                    : (double)oak_as_f32(a);
    const double db = oak_is_i32(b) ? (double)oak_as_i32(b)
                                    : (double)oak_as_f32(b);
    return da == db;
  }

  if (oak_value_tag(a) != oak_value_tag(b))
    return 0;

  switch (oak_value_tag(a))
  {
    case OAK_TAG_BOOL:   return oak_as_bool(a) == oak_as_bool(b);
    case OAK_TAG_I32:    return oak_as_i32(a) == oak_as_i32(b);
    case OAK_TAG_F32:    return oak_as_f32(a) == oak_as_f32(b);
    case OAK_TAG_NONE:   return 1;
    /* Same tag, so identity is payload equality on the packed word. */
    case OAK_TAG_NATIVE: return a.bits == b.bits;
    case OAK_TAG_HANDLE: return a.bits == b.bits;
    default:             return 0;
  }
}
