/*
 * A plugin from a future that this build does not speak.
 *
 * Refusing it is the whole point of the ABI field: the struct below may not
 * have the layout this oak expects, so the loader must not read past `abi` or
 * call anything.
 */

#include "oak_plugin.h"
#include "oak_version.h"

static int bind_nothing(oak_compile_options_t* opts)
{
  (void)opts;
  return 0;
}

static const oak_plugin_t plugin = {
  OAK_PLUGIN_ABI + 1u, "test/bad-abi", "1.0.0", OAK_VERSION_STRING,
  bind_nothing,
};

OAK_PLUGIN_EXPORT const oak_plugin_t* oak_plugin_main(void)
{
  return &plugin;
}
