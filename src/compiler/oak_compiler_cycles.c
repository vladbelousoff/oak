#include "internal/oak_compiler.h"

/* Compile-time acyclicity analysis.
 *
 * oak has no runtime cycle collector: the compiler guarantees that reference
 * counting alone reclaims every object. Two rules keep the strong ownership
 * graph acyclic:
 *
 *  1. A strong record field whose target type can reach back to the owning
 *     record (a strong cycle in the type graph) is write-once: it can only be
 *     set in a record literal. The record under construction cannot be named
 *     inside its own literal, so every such edge points at an older value and
 *     the runtime ownership graph stays a DAG. Stores into arrays/maps whose
 *     element (or key) type can reach a strong owner of that container type
 *     are rejected for the same reason (see oak_container_store_locked).
 *
 *  2. Records may not strongly own interface objects (scalar interface fields,
 *     interface-element arrays, interface-typed map keys/values): interfaces are open, so
 *     a later module can implement the interface with a record that closes a
 *     cycle this compilation cannot see. Such fields must be weak. With that
 *     rule, interface objects and interface containers are only ever owned by roots
 *     (locals, globals, the stack), never by heap objects, so root-held interface
 *     containers stay freely mutable.
 *
 * The analysis runs once per compilation, after all records and interfaces
 * (including imports and native bindings) are registered. Reachability is
 * closed-world over record types, which is sound across modules: a record's
 * field types must be visible at its declaration, so a later module can only
 * point *at* existing records, never extend what they reach — except through
 * interfaces, which rule 2 removes from the strong graph entirely.
 */

static int record_index_by_id(const oak_record_registry_t* r,
                              oak_type_id_t type_id)
{
  if (type_id == OAK_TYPE_VOID)
    return -1;
  const oak_registered_record_t* entries =
      OAK_CDATA(oak_registered_record_t, r->entries);
  for (usize i = 0; i < oak_size(r->entries); ++i)
  {
    if (entries[i].type_id == type_id && !entries[i].is_value)
      return (int)i;
  }
  return -1;
}

static int type_is_interface_id(const oak_compiler_t* c, oak_type_id_t id)
{
  return oak_interface_find_by_id(&c->interfaces, id) != null;
}

/* True if a strong slot of this type holds an interface object, directly or as a
 * container element/key. Covers fields declared before their (local) interface
 * was registered, which lower as SCALAR with the interface's interned id. */
static int type_holds_interface(const oak_compiler_t* c,
                            const oak_type_t* t)
{
  if (t->kind == OAK_TYPE_KIND_FN)
    return 0;
  if (type_is_interface_id(c, t->id))
    return 1;
  if (t->kind == OAK_TYPE_KIND_MAP && type_is_interface_id(c, t->key_id))
    return 1;
  return 0;
}

/* Collects the record-registry indices a strong slot of type `t` can own:
 * the scalar/element/value type, plus the key type for maps. Returns the
 * count (0, 1, or 2). Leaves (numbers, strings, enums, fns, value types)
 * and interfaces contribute nothing. */
static int type_record_targets(const oak_compiler_t* c,
                               const oak_type_t* t,
                               int out[2])
{
  int n = 0;
  if (t->kind == OAK_TYPE_KIND_FN || t->kind == OAK_TYPE_KIND_INTERFACE)
    return 0;
  const int j = record_index_by_id(&c->records, t->id);
  if (j >= 0)
    out[n++] = j;
  if (t->kind == OAK_TYPE_KIND_MAP)
  {
    const int k = record_index_by_id(&c->records, t->key_id);
    if (k >= 0)
      out[n++] = k;
  }
  return n;
}

/* Finds the type node token of field `field_name` on the program's local
 * declaration of record `record_name`, for error locations. Returns null for
 * imported and native records. */
static const oak_token_t*
field_decl_token(const oak_ast_node_t* program,
                 const char* record_name,
                 const char* field_name)
{
  if (!program)
    return null;
  oak_list_entry_t* pos;
  oak_list_for_each(pos, &program->children)
  {
    const oak_ast_node_t* item = oak_unwrap_decl(
        oak_container_of(pos, oak_ast_node_t, link));
    if (!item || item->kind != OAK_NODE_RECORD_DECL || !item->lhs || !item->rhs)
      continue;

    const oak_ast_node_t* name_ident = item->lhs;
    if (name_ident->kind == OAK_NODE_TYPE_NAME)
    {
      const oak_list_entry_t* first = name_ident->children.next;
      if (first == &name_ident->children)
        continue;
      name_ident = oak_container_of(first, oak_ast_node_t, link);
    }
    if (name_ident->kind != OAK_NODE_IDENT ||
        strcmp(oak_token_text(name_ident->token), record_name) != 0)
      continue;

    oak_list_entry_t* fpos;
    oak_list_for_each(fpos, &item->rhs->children)
    {
      const oak_ast_node_t* fdecl =
          oak_container_of(fpos, oak_ast_node_t, link);
      if (fdecl->kind != OAK_NODE_RECORD_FIELD_DECL || !fdecl->lhs)
        continue;
      if (fdecl->lhs->kind == OAK_NODE_IDENT &&
          strcmp(oak_token_text(fdecl->lhs->token), field_name) == 0)
        return fdecl->lhs->token;
    }
    return null;
  }
  return null;
}

void oak_compiler_check_cycles(oak_compiler_t* c,
                               const oak_ast_node_t* program)
{
  const int n = (int)oak_size(c->records.entries);
  /* Re-runnable: attribute callbacks bind new native types after the first
   * pass, and the matrix has to be rebuilt at the new size rather than left
   * describing a smaller registry. */
  if (c->cycle_reach)
    OAK_FREE(c->allocator, c->cycle_reach);
  c->cycle_reach = null;
  c->cycle_reach_count = n;
  if (n == 0)
    return;

  u8* reach = OAK_ALLOC(c->allocator, (usize)n * (usize)n);
  memset(reach, 0, (usize)n * (usize)n);
  c->cycle_reach = reach;

  /* Direct strong edges, plus the interface-ownership rule. */
  const oak_registered_record_t* records =
      OAK_CDATA(oak_registered_record_t, c->records.entries);
  for (int i = 0; i < n; ++i)
  {
    const oak_registered_record_t* sd = &records[i];
    if (sd->is_value)
      continue;
    reach[i * n + i] = 1;
    const oak_record_field_t* fields =
        OAK_CDATA(oak_record_field_t, sd->fields);
    for (usize fi = 0; fi < oak_size(sd->fields); ++fi)
    {
      const oak_record_field_t* f = &fields[fi];
      if (f->type.is_weak)
        continue;

      if (type_holds_interface(c, &f->type))
      {
        oak_compiler_error_at(
            c,
            field_decl_token(program, sd->name, f->name),
            "field '%s' of record '%s' strongly owns an interface object; a later "
            "interface impl could close a reference cycle — declare the field "
            "weak ('%s weak')",
            f->name,
            sd->name,
            oak_type_full_name(c, f->type));
        continue;
      }

      int targets[2];
      const int tc = type_record_targets(c, &f->type, targets);
      for (int t = 0; t < tc; ++t)
        reach[i * n + targets[t]] = 1;
    }
  }
  for (int i = 0; i < n; ++i)
    reach[i * n + i] = 1;

  /* Reflexive transitive closure (record counts stay small). */
  for (int k = 0; k < n; ++k)
    for (int i = 0; i < n; ++i)
    {
      if (!reach[i * n + k])
        continue;
      for (int j = 0; j < n; ++j)
        if (reach[k * n + j])
          reach[i * n + j] = 1;
    }

  /* A strong field is write-once when its target can own its owner back. */
  oak_registered_record_t* mut_records =
      OAK_DATA(oak_registered_record_t, c->records.entries);
  for (int i = 0; i < n; ++i)
  {
    oak_registered_record_t* sd = &mut_records[i];
    if (sd->is_value)
      continue;
    oak_record_field_t* fields =
        OAK_DATA(oak_record_field_t, sd->fields);
    for (usize fi = 0; fi < oak_size(sd->fields); ++fi)
    {
      oak_record_field_t* f = &fields[fi];
      if (f->type.is_weak)
        continue;
      int targets[2];
      const int tc = type_record_targets(c, &f->type, targets);
      for (int t = 0; t < tc; ++t)
      {
        if (reach[targets[t] * n + i])
        {
          f->cycle_locked = 1;
          break;
        }
      }
    }
  }
}

void oak_compiler_free_cycles(oak_compiler_t* c)
{
  if (c->cycle_reach)
    OAK_FREE(c->allocator, c->cycle_reach);
  c->cycle_reach = null;
  c->cycle_reach_count = 0;
}

/* True if `field` strongly owns a container with the same base type as
 * `coll` (weak container fields do not own their target). */
static int field_owns_container(const oak_record_field_t* field,
                                const oak_type_t* coll)
{
  if (field->type.is_weak)
    return 0;
  if (field->type.kind != coll->kind || field->type.id != coll->id)
    return 0;
  if (coll->kind == OAK_TYPE_KIND_MAP && field->type.key_id != coll->key_id)
    return 0;
  return 1;
}

int oak_container_store_locked(oak_compiler_t* c,
                               const oak_type_t* coll)
{
  if (coll->kind != OAK_TYPE_KIND_ARRAY && coll->kind != OAK_TYPE_KIND_MAP)
    return 0;
  const int n = c->cycle_reach_count;
  if (!c->cycle_reach || n == 0)
    return 0;

  /* Interface elements never lock a store: records cannot strongly own interface
   * objects or interface containers, so the stored value can never reach this
   * container through the heap. Record elements lock the store when they can
   * reach a record that strongly owns a container of this exact type. */
  int targets[2];
  const int tc = type_record_targets(c, coll, targets);
  const oak_registered_record_t* records =
      OAK_CDATA(oak_registered_record_t, c->records.entries);
  for (int t = 0; t < tc; ++t)
  {
    /* targets[t] indexes the record registry as it stands now, but the matrix
     * was sized when the analysis ran. An attribute callback can bind new
     * native types afterwards, which appends records -- so a target can be
     * past the last row. oak_compiler_check_cycles is re-run when that
     * happens; this guard is what keeps the read in bounds if it ever is not.
     */
    if (targets[t] >= n)
      continue;
    const u8* row = c->cycle_reach + (usize)targets[t] * (usize)n;
    for (int k = 0; k < n; ++k)
    {
      if (!row[k])
        continue;
      const oak_registered_record_t* sd = &records[k];
      if (sd->is_value)
        continue;
      const oak_record_field_t* fields =
          OAK_CDATA(oak_record_field_t, sd->fields);
      for (usize fi = 0; fi < oak_size(sd->fields); ++fi)
        if (field_owns_container(&fields[fi], coll))
          return 1;
    }
  }
  return 0;
}
