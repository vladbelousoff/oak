#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Discriminates the three shapes a compile-time type slot can have. The
 * default (zero) value is OAK_TYPE_KIND_SCALAR so that zero-initialised
 * type slots are valid scalar types without explicit assignment.
 *
 * Public because oak_bind_type_ref_t (oak_bind.h) carries one: it is how an
 * embedder says whether a parameter, return, or field type is a plain value,
 * an array, or a map. */
typedef enum oak_type_kind oak_type_kind_t;
enum oak_type_kind
{
  OAK_TYPE_KIND_SCALAR = 0, /* plain value: number, bool, string, user record */
  OAK_TYPE_KIND_ARRAY,      /* typed array; element type is `id` */
  OAK_TYPE_KIND_MAP, /* typed map; key type is `key_id`, value type is `id` */
  OAK_TYPE_KIND_INTERFACE, /* interface object; interface type id is `id` */
  OAK_TYPE_KIND_FN,        /* function value */
};

#ifdef __cplusplus
}
#endif
