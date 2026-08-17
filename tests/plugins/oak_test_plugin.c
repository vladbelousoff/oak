/*
 * A minimal, well-formed plugin, for the host loader's tests.
 *
 * It registers nothing: what is under test here is the handshake -- that the
 * entry point is found, the ABI and oak version are checked, and the package
 * name is matched -- not what binding does, which the bind_fn and bind_type
 * suites already cover.
 */

#include "oak_plugin.h"
#include "oak_version.h"

static int bind_nothing(oak_compile_options_t* opts)
{
  (void)opts;
  return 0;
}

static const oak_plugin_t plugin = {
  OAK_PLUGIN_ABI, "test/plugin", "1.0.0", OAK_VERSION_STRING, bind_nothing,
};

OAK_PLUGIN_EXPORT const oak_plugin_t* oak_plugin_main(void)
{
  return &plugin;
}
