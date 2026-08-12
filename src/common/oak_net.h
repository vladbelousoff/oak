#pragma once

#include "oak_types.h"

typedef struct oak_net_socket oak_net_socket_t;
struct oak_net_socket
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
                            oak_net_socket_t* out);
int oak_net_accept(oak_net_socket_t listener,
                   oak_net_socket_t* out);
void oak_net_close(oak_net_socket_t socket);

/* Wait until the socket is readable. timeout_ms < 0 blocks indefinitely;
 * zero polls; positive values wait up to the given duration. */
int oak_net_wait_readable(oak_net_socket_t socket, int timeout_ms);
int oak_net_recv(oak_net_socket_t socket, void* data, usize capacity);
int oak_net_send_all(oak_net_socket_t socket,
                     const void* data,
                     usize size);
