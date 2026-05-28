#pragma once

#include "oak_defs.h"
#include "oak_type_id.h"

struct oak_allocator_t;

#define OAK_MAX_GENERIC_PARAMS 8

struct oak_generic_param_t
{
  const char* name;
  oak_type_id_t bound_trait_id; /* OAK_TYPE_VOID if unbounded */
};

struct oak_generic_def_t
{
  const char* owner_name;
  struct oak_generic_param_t* params;
  int param_count;
};

struct oak_generic_registry_t
{
  struct oak_allocator_t* allocator;
  struct oak_generic_def_t* defs;
  int def_count;
  int def_capacity;
};

void oak_generic_registry_init(struct oak_generic_registry_t* r,
                               struct oak_allocator_t* allocator);
void oak_generic_registry_free(struct oak_generic_registry_t* r);

int oak_generic_registry_add(struct oak_generic_registry_t* r,
                             const struct oak_generic_def_t* def);
