#include "oak_version.h"

/* These report the library's own build, not the caller's headers: both strings
 * are baked in when acorn is compiled. A caller comparing OAK_VERSION_STRING
 * against oak_version() is comparing "what I compiled against" with "what I am
 * linked to", which is the whole point of having both. */

const char* oak_version(void)
{
  return OAK_VERSION_STRING;
}

const char* oak_platform(void)
{
  return OAK_PLATFORM;
}
