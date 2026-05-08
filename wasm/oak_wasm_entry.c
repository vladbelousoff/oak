#include <emscripten/emscripten.h>

int oak_run(const char* code);

EMSCRIPTEN_KEEPALIVE
int oak_run_wrapper(const char* code)
{
  return oak_run(code);
}
