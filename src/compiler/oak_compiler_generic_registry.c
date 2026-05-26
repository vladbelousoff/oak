#include "internal/oak_compiler.h"

void oak_generic_registry_init(struct oak_generic_registry_t* r,
                               struct oak_allocator_t* allocator)
{
  r->allocator = allocator;
  r->defs = null;
  r->def_count = 0;
  r->def_capacity = 0;
}

void oak_generic_registry_free(struct oak_generic_registry_t* r)
{
  for (int i = 0; i < r->def_count; ++i)
  {
    for (int j = 0; j < r->defs[i].param_count; ++j)
    {
      if (r->defs[i].params[j].name)
        OAK_FREE(r->allocator, (void*)r->defs[i].params[j].name);
    }
    if (r->defs[i].params)
      OAK_FREE(r->allocator, r->defs[i].params);
  }
  if (r->defs)
    OAK_FREE(r->allocator, r->defs);
  r->defs = null;
  r->def_count = 0;
  r->def_capacity = 0;
}

int oak_generic_registry_add(struct oak_generic_registry_t* r,
                             const struct oak_generic_def_t* def)
{
  oak_dynarr_push(r->allocator,
                  &r->defs,
                  &r->def_count,
                  &r->def_capacity,
                  def,
                  sizeof(*def));
  return r->def_count - 1;
}
