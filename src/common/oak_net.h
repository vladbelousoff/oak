#pragma once

#include "oak_types.h"

struct oak_net_socket_t
{
  usize handle;
};

#define OAK_NET_INVALID ((usize) - 1)

/* Process-level socket initialization/cleanup. No-ops on POSIX; required by
 * Winsock on Windows. */
int oak_net_init(void);
void oak_net_shutdown(void);

/* Create a TCP listener bound only to 127.0.0.1. A requested port of zero
 * selects an available ephemeral port. */
int oak_net_listen_loopback(int requested_port,
                            int* actual_port,
                            struct oak_net_socket_t* out);
int oak_net_accept(struct oak_net_socket_t listener,
                   struct oak_net_socket_t* out);
void oak_net_close(struct oak_net_socket_t socket);

/* Wait until the socket is readable. timeout_ms < 0 blocks indefinitely;
 * zero polls; positive values wait up to the given duration. */
int oak_net_wait_readable(struct oak_net_socket_t socket, int timeout_ms);
int oak_net_recv(struct oak_net_socket_t socket, void* data, usize capacity);
int oak_net_send_all(struct oak_net_socket_t socket,
                     const void* data,
                     usize size);
