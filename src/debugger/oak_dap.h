#pragma once

#include "oak_debugger.h"

typedef struct oak_chunk oak_chunk_t;
typedef struct oak_vm oak_vm_t;

/* Serve one localhost Debug Adapter Protocol session and run `chunk` after
 * the client sends configurationDone. Returns the VM result, or
 * OAK_VM_DEBUG_HALT when the session is terminated by the client. */
OAK_API oak_vm_result_t oak_dap_serve(oak_debugger_t* dbg,
                                           oak_vm_t* vm,
                                           oak_chunk_t* chunk,
                                           int port);

/* Called by oak_debugger_hook while a DAP session owns the debugger. */
void oak_dap_poll(oak_debugger_t* dbg, oak_vm_t* vm);
void oak_dap_stopped(oak_debugger_t* dbg,
                     oak_vm_t* vm,
                     const char* reason);
