#pragma once

#define OAK_MAX_DIAGNOSTICS 64

struct oak_diagnostic_t
{
  int line;   /* 0 = no source location */
  int column;
  char message[512];
};
