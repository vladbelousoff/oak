#include "oak_bind.h"

#include "oak_allocator.h"
#include "oak_log.h"
#include "oak_str.h"
#include "oak_type.h"
#include "oak_value_impl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/* Record why a binding was rejected, so oak_compile_ex can report it instead
 * of the compile failing much later at the first use of the missing name.
 * Always returns -1, which is what every caller returns. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static int bind_reject(oak_compile_options_t* opts, const char* fmt, ...)
{
  if (!opts || !opts->bind_errors)
    return -1;

  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const usize len = strlen(buf) + 1u;
  char* copy = OAK_ALLOC(opts->allocator, len);
  if (!copy)
    return -1;
  memcpy(copy, buf, len);
  oak_assert(oak_push_back(opts->bind_errors, &copy));
  return -1;
}

void oak_lower_bind_ref(const struct oak_bind_type_ref* r, oak_type_t* out)
{
  oak_type_clear(out);
  out->kind = r->kind;
  if (r->type)
    out->id = r->type->resolved_type_id;
  else if (r->enum_type)
    out->id = r->enum_type->resolved_type_id;
  else
    out->id = r->id;
  if (r->kind == OAK_TYPE_KIND_MAP)
    out->key_id = r->key_type ? r->key_type->resolved_type_id : r->key_id;
  out->is_weak = r->is_weak;
}

void oak_compile_options_init(oak_compile_options_t* opts,
                             oak_allocator_t* allocator)
{
  if (!opts)
    return;
  opts->allocator = allocator;
  opts->source_name = null;
  opts->native_types = oak_vector_new(allocator,
                                         sizeof(oak_bind_type_t*));
  opts->native_fns = oak_vector_new(allocator,
                                       sizeof(oak_bind_fn_t));
  opts->native_global_fns =
      oak_vector_new(allocator, sizeof(oak_bind_global_fn_t));
  opts->native_enums = oak_vector_new(allocator,
                                         sizeof(oak_bind_enum_t*));
  opts->native_attrs = oak_vector_new(allocator,
                                         sizeof(oak_bind_attr_t));
  opts->bind_errors = oak_vector_new(allocator, sizeof(char*));
  oak_assert(opts->native_types && opts->native_fns &&
             opts->native_global_fns && opts->native_enums &&
             opts->native_attrs && opts->bind_errors);
  opts->emit_debug_info = 1;
  opts->module_registry = null;
  opts->current_module = null;
  opts->allow_bodyless_fns = 0;
  opts->allow_synthetic_native_modules = 0;
}

void oak_compile_options_free(oak_compile_options_t* opts)
{
  if (!opts)
    return;
  oak_bind_type_t** types =
      OAK_DATA(oak_bind_type_t*, opts->native_types);
  for (usize i = 0; i < oak_size(opts->native_types); ++i)
  {
    const oak_bind_field_t* fields =
        OAK_CDATA(oak_bind_field_t, types[i]->fields);
    for (usize fi = 0; fi < oak_size(types[i]->fields); ++fi)
      OAK_FREE(opts->allocator, (void*)fields[fi].name);
    oak_destroy(types[i]->fields);
    OAK_FREE(opts->allocator, types[i]);
  }
  oak_destroy(opts->native_types);
  opts->native_types = null;
  oak_destroy(opts->native_fns);
  opts->native_fns = null;
  oak_destroy(opts->native_global_fns);
  opts->native_global_fns = null;

  oak_bind_enum_t** enums =
      OAK_DATA(oak_bind_enum_t*, opts->native_enums);
  for (usize i = 0; i < oak_size(opts->native_enums); ++i)
  {
    oak_destroy(enums[i]->variants);
    OAK_FREE(opts->allocator, enums[i]);
  }
  oak_destroy(opts->native_enums);
  opts->native_enums = null;
  oak_destroy(opts->native_attrs);
  opts->native_attrs = null;

  char** bind_errors = OAK_DATA(char*, opts->bind_errors);
  for (usize i = 0; i < oak_size(opts->bind_errors); ++i)
    OAK_FREE(opts->allocator, bind_errors[i]);
  oak_destroy(opts->bind_errors);
  opts->bind_errors = null;
}


oak_bind_type_t* oak_bind_type(oak_compile_options_t* opts,
                                      const oak_bind_type_kind_t kind,
                                      const char* name)
{
  return oak_bind_type_in_module(opts, null, kind, name);
}

oak_bind_type_t* oak_bind_type_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    const oak_bind_type_kind_t kind,
    const char* name)
{
  if (!opts || !name)
    return null;

  oak_bind_type_t* t =
      OAK_ALLOC(opts->allocator, sizeof(oak_bind_type_t));
  t->module_name = module_name;
  t->kind = kind;
  t->name = name;
  t->resolved_type_id = OAK_TYPE_VOID;
  t->allocator = opts->allocator;
  t->fields = oak_vector_new(t->allocator, sizeof(oak_bind_field_t));
  oak_assert(t->fields);
  t->opts = opts;
  t->destructor = null;
  t->user_data = null;

  oak_assert(oak_push_back(opts->native_types, &t));
  return t;
}

int oak_bind_field(oak_bind_type_t* type,
                   const oak_bind_field_t* p)
{
  if (!type || !p)
    return -1;
  if (!p->name)
    return bind_reject(type->opts, "a field on '%s' has no name", type->name);
  if (!p->getter)
    return bind_reject(
        type->opts, "field '%s.%s' has no getter", type->name, p->name);
  /* Inline value types have no runtime type identity, so field access cannot
   * be dispatched on them; they expose data through methods only. */
  if (type->kind == OAK_BIND_TYPE_VALUE)
    return bind_reject(type->opts,
                       "value type '%s' cannot declare the field '%s'; "
                       "value types expose data through methods only",
                       type->name,
                       p->name);

  /* Reject duplicate field names. */
  const oak_bind_field_t* fields =
      OAK_CDATA(oak_bind_field_t, type->fields);
  for (usize i = 0; i < oak_size(type->fields); ++i)
  {
    if (strcmp(fields[i].name, p->name) == 0)
      return bind_reject(
          type->opts, "duplicate field '%s.%s'", type->name, p->name);
  }

  const usize len = strlen(p->name) + 1u;
  char* name_copy = OAK_ALLOC(type->allocator, len);
  if (!name_copy)
    return -1;
  memcpy(name_copy, p->name, len);

  oak_bind_field_t f = {
    .name = name_copy,
    .type = p->type,
    .getter = p->getter,
    .setter = p->setter,
    .user_data = p->user_data,
  };
  oak_assert(oak_push_back(type->fields, &f));
  return 0;
}

int oak_bind_fn_global(oak_compile_options_t* opts,
                       const oak_bind_global_fn_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name)
    return bind_reject(opts, "a global native function has no name");
  if (!p->impl)
    return bind_reject(opts, "native function '%s' has no implementation",
                       p->name);
  if (p->arity > OAK_MAX_ARITY)
    return bind_reject(opts,
                       "native function '%s' declares arity %zu, above the "
                       "maximum of %u",
                       p->name,
                       p->arity,
                       (unsigned)OAK_MAX_ARITY);
  if (p->param_types && p->param_count != p->arity)
    return bind_reject(opts,
                       "native function '%s' declares arity %zu but %zu "
                       "parameter types; use OAK_BIND_PARAMS to state it once",
                       p->name,
                       p->arity,
                       p->param_count);
  oak_bind_global_fn_t entry = *p;
  oak_assert(oak_push_back(opts->native_global_fns, &entry));
  return 0;
}

int oak_bind_fn(oak_compile_options_t* opts,
                const oak_bind_fn_t* p)
{
  if (!opts || !p)
    return -1;
  if (!p->name)
    return bind_reject(opts, "a native method has no name");
  if (!p->impl)
    return bind_reject(opts, "native method '%s' has no implementation",
                       p->name);
  if (p->arity > OAK_MAX_ARITY)
    return bind_reject(opts,
                       "native method '%s' declares arity %zu, above the "
                       "maximum of %u",
                       p->name,
                       p->arity,
                       (unsigned)OAK_MAX_ARITY);
  if (p->param_types && p->param_count != p->arity)
    return bind_reject(opts,
                       "native method '%s' declares arity %zu but %zu "
                       "parameter types; use OAK_BIND_PARAMS to state it once",
                       p->name,
                       p->arity,
                       p->param_count);
  if (p->kind != OAK_BIND_FN_INSTANCE_METHOD &&
      p->kind != OAK_BIND_FN_STATIC_METHOD)
    return bind_reject(
        opts, "native method '%s' has an unknown binding kind", p->name);
  if (!p->receiver_type)
    return bind_reject(
        opts, "native method '%s' names no receiver type", p->name);

  oak_bind_fn_t copy = *p;
  oak_assert(oak_push_back(opts->native_fns, &copy));
  return 0;
}

oak_bind_enum_t* oak_bind_enum(oak_compile_options_t* opts,
                                      const char* name)
{
  return oak_bind_enum_in_module(opts, null, name);
}

oak_bind_enum_t* oak_bind_enum_in_module(
    oak_compile_options_t* opts,
    const char* module_name,
    const char* name)
{
  if (!opts || !name)
    return null;

  oak_bind_enum_t* e =
      OAK_ALLOC(opts->allocator, sizeof(oak_bind_enum_t));
  e->module_name = module_name;
  e->name = name;
  e->resolved_type_id = OAK_TYPE_VOID;
  e->opts = opts;
  e->allocator = opts->allocator;
  e->variants =
      oak_vector_new(e->allocator, sizeof(oak_bind_enum_variant_t));
  oak_assert(e->variants);

  oak_assert(oak_push_back(opts->native_enums, &e));
  return e;
}

int oak_bind_enum_variant(oak_bind_enum_t* e,
                          const char* name,
                          const int value)
{
  if (!e || !name)
    return -1;

  const oak_bind_enum_variant_t* variants =
      OAK_CDATA(oak_bind_enum_variant_t, e->variants);
  for (usize i = 0; i < oak_size(e->variants); ++i)
  {
    if (strcmp(variants[i].name, name) == 0)
      return bind_reject(
          e->opts, "duplicate variant '%s.%s'", e->name, name);
  }

  oak_bind_enum_variant_t v = {
    .name = name,
    .value = value,
  };
  oak_assert(oak_push_back(e->variants, &v));
  return 0;
}


oak_value_t oak_native_record_new(oak_allocator_t* allocator,
                                         const oak_bind_type_t* type,
                                         void* instance)
{
  oak_assert(type != null);
  oak_obj_native_record_t* ns =
      oak_obj_native_record_new(allocator, type, instance);
  return OAK_VALUE_OBJ(&ns->obj);
}

void* oak_native_instance(const oak_value_t value)
{
  return oak_as_native_record(value)->instance;
}

oak_value_t oak_native_value_new(void* payload)
{
  return oak_value_native(payload);
}

void* oak_native_value(const oak_value_t value)
{
  return oak_as_native_value(value);
}

void oak_dispatch_compile_attr_cbs(const oak_compile_options_t* opts,
                                   const char** attrs,
                                   int attr_count,
                                   const char* decl_name,
                                   oak_attr_target_t target,
                                   const oak_attr_param_info_t* params,
                                   int param_count,
                                   const oak_attr_field_info_t* fields,
                                   int field_count,
                                   int const_index)
{
  if (!opts || oak_size(opts->native_attrs) == 0 || attr_count == 0)
    return;
  const oak_bind_attr_t* attr_bindings =
      OAK_CDATA(oak_bind_attr_t, opts->native_attrs);
  for (usize bi = 0; bi < oak_size(opts->native_attrs); ++bi)
  {
    const oak_bind_attr_t* b = &attr_bindings[bi];
    if (!b->on_decl)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        oak_attr_compile_ctx_t ctx = {
          .target = target,
          .decl_name = decl_name,
          .user_data = b->user_data,
          .param_count = param_count,
          .params = params,
          .field_count = field_count,
          .fields = fields,
          .const_index = const_index,
          /* Expose the live options so callbacks can bind new native
           * types/methods that the post-record-decl native pass registers.
           * Cast away const: opts is the compiler's working options object
           * (never a read-only literal) and the binding API mutates it. */
          .opts = (oak_compile_options_t*)opts,
        };
        b->on_decl(&ctx);
        break;
      }
    }
  }
}

void oak_apply_attr_hooks(const oak_compile_options_t* opts,
                          oak_obj_fn_t* fn_obj,
                          oak_obj_native_fn_t* native_obj,
                          const char** attrs,
                          int attr_count)
{
  if (!opts || oak_size(opts->native_attrs) == 0 || attr_count == 0)
    return;

  int match_count = 0;
  const oak_bind_attr_t* attr_bindings =
      OAK_CDATA(oak_bind_attr_t, opts->native_attrs);
  for (usize bi = 0; bi < oak_size(opts->native_attrs); ++bi)
  {
    const oak_bind_attr_t* b = &attr_bindings[bi];
    if (!b->on_call)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        ++match_count;
        break;
      }
    }
  }
  if (match_count == 0)
    return;

  oak_allocator_t* a = fn_obj ? fn_obj->obj.allocator
                             : native_obj ? native_obj->obj.allocator
                             : null;
  if (!a)
    return;
  oak_attr_hook_entry_t* hooks = OAK_ALLOC(a,
      (usize)match_count * sizeof(oak_attr_hook_entry_t));
  int idx = 0;
  for (usize bi = 0; bi < oak_size(opts->native_attrs); ++bi)
  {
    const oak_bind_attr_t* b = &attr_bindings[bi];
    if (!b->on_call)
      continue;
    for (int ai = 0; ai < attr_count; ++ai)
    {
      if (strcmp(b->name, attrs[ai]) == 0)
      {
        hooks[idx].cb = b->on_call;
        hooks[idx].ud = b->user_data;
        ++idx;
        break;
      }
    }
  }

  if (fn_obj)
  {
    fn_obj->attr_hooks = hooks;
    fn_obj->attr_hook_count = match_count;
  }
  else if (native_obj)
  {
    native_obj->attr_hooks = hooks;
    native_obj->attr_hook_count = match_count;
  }
  else
  {
    OAK_FREE(a, hooks);
  }
}

int oak_bind_attr(oak_compile_options_t* opts,
                  const oak_bind_attr_t* params)
{
  if (!opts || !params || !params->name)
    return -1;
  oak_assert(oak_push_back(opts->native_attrs, params));
  return 0;
}

int oak_bind_fields(oak_bind_type_t* type,
                    const oak_bind_field_t* fields,
                    const int count)
{
  if (!type || (count > 0 && !fields))
    return -1;
  int rc = 0;
  for (int i = 0; i < count; ++i)
  {
    if (oak_bind_field(type, &fields[i]) != 0)
      rc = -1;
  }
  return rc;
}

int oak_bind_fns(oak_compile_options_t* opts,
                 const oak_bind_fn_t* fns,
                 const int count)
{
  if (!opts || (count > 0 && !fns))
    return -1;
  int rc = 0;
  for (int i = 0; i < count; ++i)
  {
    if (oak_bind_fn(opts, &fns[i]) != 0)
      rc = -1;
  }
  return rc;
}

int oak_bind_fns_global(oak_compile_options_t* opts,
                        const oak_bind_global_fn_t* fns,
                        const int count)
{
  if (!opts || (count > 0 && !fns))
    return -1;
  int rc = 0;
  for (int i = 0; i < count; ++i)
  {
    if (oak_bind_fn_global(opts, &fns[i]) != 0)
      rc = -1;
  }
  return rc;
}

int oak_bind_enum_variants(oak_bind_enum_t* e,
                           const oak_bind_enum_variant_t* variants,
                           const int count)
{
  if (!e || (count > 0 && !variants))
    return -1;
  int rc = 0;
  for (int i = 0; i < count; ++i)
  {
    if (oak_bind_enum_variant(e, variants[i].name, variants[i].value) != 0)
      rc = -1;
  }
  return rc;
}

int oak_value_matches(const oak_value_t value, const oak_bind_type_ref_t ref)
{
  switch (ref.kind)
  {
    case OAK_TYPE_KIND_ARRAY:
      return oak_is_array(value);
    case OAK_TYPE_KIND_MAP:
      return oak_is_map(value);
    case OAK_TYPE_KIND_INTERFACE:
      return oak_is_interface_object(value);
    case OAK_TYPE_KIND_FN:
      return oak_is_fn(value) || oak_is_native_fn(value);
    case OAK_TYPE_KIND_SCALAR:
      break;
  }

  /* A custom descriptor names either a native record or an inline value type;
   * neither carries its descriptor in a form comparable here, so match on the
   * representation the binding produces. */
  if (ref.type)
  {
    return ref.type->kind == OAK_BIND_TYPE_VALUE ? oak_is_native_value(value)
                                                 : oak_is_native_record(value);
  }

  /* An enum lowers to its integer value at run time, so there is nothing
   * narrower to check than "is an integer". The compiler is what enforces that
   * the integer came from the right enum. */
  if (ref.enum_type)
    return oak_is_i32(value);

  switch (ref.id)
  {
    case OAK_TYPE_VOID:
      return 1; /* unspecified: accepts anything */
    case OAK_TYPE_NUMBER:
      return oak_is_number(value);
    case OAK_TYPE_STRING:
      return oak_is_string(value);
    case OAK_TYPE_BOOL:
      return oak_is_bool(value);
    case OAK_TYPE_FN:
      return oak_is_fn(value) || oak_is_native_fn(value);
    case OAK_TYPE_NONE:
      return oak_is_none_like(value);
    default:
      /* A user-declared record type from Oak source. */
      return oak_is_record(value) || oak_is_native_record(value);
  }
}

int oak_native_args_match(const oak_value_t* args,
                          const usize argc,
                          const oak_bind_type_ref_t* types,
                          const usize count)
{
  if (argc != count)
    return 0;
  if (count > 0 && (!args || !types))
    return 0;
  for (usize i = 0; i < count; ++i)
  {
    if (!oak_value_matches(args[i], types[i]))
      return 0;
  }
  return 1;
}
