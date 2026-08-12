#pragma once

#define OAK_MAX_DIAGNOSTICS 64

typedef struct oak_diagnostic oak_diagnostic_t;
struct oak_diagnostic
{
  int line; /* 0 = no source location */
  int column;
  char message[512];
};
