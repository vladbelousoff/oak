#pragma once

#include "oak_debugger.h"

struct oak_chunk_t;
struct oak_vm_t;

/* Serve one localhost Debug Adapter Protocol session and run `chunk` after
 * the client sends configurationDone. Returns the VM result, or
 * OAK_VM_DEBUG_HALT when the session is terminated by the client. */
OAK_API enum oak_vm_result_t oak_dap_serve(struct oak_debugger_t* dbg,
                                           struct oak_vm_t* vm,
                                           struct oak_chunk_t* chunk,
                                           int port);

/* Called by oak_debugger_hook while a DAP session owns the debugger. */
void oak_dap_poll(struct oak_debugger_t* dbg, struct oak_vm_t* vm);
void oak_dap_stopped(struct oak_debugger_t* dbg,
                     struct oak_vm_t* vm,
                     const char* reason);
