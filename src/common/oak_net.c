#include "oak_net.h"

#include "oak_log.h"

#include <errno.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET oak_native_socket_t;
#define oak_native_close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int oak_native_socket_t;
#define oak_native_close close
#endif

static oak_native_socket_t native_socket(const struct oak_net_socket_t socket)
{
  return (oak_native_socket_t)socket.handle;
}

static void log_socket_error(const char* action)
{
#if defined(_WIN32)
  oak_log(OAK_LOG_ERROR, "%s (Winsock error %d)", action, WSAGetLastError());
#else
  oak_log(OAK_LOG_ERROR, "%s: %s", action, strerror(errno));
#endif
}

int oak_net_init(void)
{
#if defined(_WIN32)
  WSADATA data;
  const int result = WSAStartup(MAKEWORD(2, 2), &data);
  if (result != 0)
  {
    oak_log(OAK_LOG_ERROR, "could not initialize Winsock (error %d)", result);
    return 0;
  }
  return 1;
#else
  return 1;
#endif
}

void oak_net_shutdown(void)
{
#if defined(_WIN32)
  WSACleanup();
#endif
}

int oak_net_listen_loopback(const int requested_port,
                            int* actual_port,
                            struct oak_net_socket_t* out)
{
  const oak_native_socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
  if (server == INVALID_SOCKET)
#else
  if (server < 0)
#endif
  {
    log_socket_error("could not create socket");
    return 0;
  }

#if defined(__APPLE__)
  const int no_sigpipe = 1;
  if (setsockopt(
          server, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) !=
      0)
  {
    log_socket_error("could not configure socket");
    oak_native_close(server);
    return 0;
  }
#endif

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)requested_port);
  if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) != 0)
  {
    log_socket_error("could not bind socket");
    oak_native_close(server);
    return 0;
  }
  if (listen(server, 1) != 0)
  {
    log_socket_error("could not listen on socket");
    oak_native_close(server);
    return 0;
  }

#if defined(_WIN32)
  int len = sizeof(addr);
#else
  socklen_t len = sizeof(addr);
#endif
  if (getsockname(server, (struct sockaddr*)&addr, &len) != 0)
  {
    log_socket_error("could not query socket address");
    oak_native_close(server);
    return 0;
  }
  *actual_port = ntohs(addr.sin_port);
  out->handle = (usize)server;
  return 1;
}

int oak_net_accept(const struct oak_net_socket_t listener,
                   struct oak_net_socket_t* out)
{
  const oak_native_socket_t client =
      accept(native_socket(listener), null, null);
#if defined(_WIN32)
  if (client == INVALID_SOCKET)
#else
  if (client < 0)
#endif
  {
    log_socket_error("could not accept socket connection");
    return 0;
  }
  out->handle = (usize)client;
  return 1;
}

void oak_net_close(const struct oak_net_socket_t socket)
{
  if (socket.handle != OAK_NET_INVALID)
    oak_native_close(native_socket(socket));
}

int oak_net_wait_readable(const struct oak_net_socket_t socket,
                          const int timeout_ms)
{
  const oak_native_socket_t native = native_socket(socket);
  fd_set reads;
  FD_ZERO(&reads);
  FD_SET(native, &reads);
  struct timeval tv;
  struct timeval* tv_ptr = null;
  if (timeout_ms >= 0)
  {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    tv_ptr = &tv;
  }
  const int result = select((int)native + 1, &reads, null, null, tv_ptr);
  if (result < 0)
    log_socket_error("socket readiness check failed");
  return result;
}

int oak_net_recv(const struct oak_net_socket_t socket,
                 void* data,
                 const usize capacity)
{
  const int result = recv(native_socket(socket), data, (int)capacity, 0);
  if (result < 0)
    log_socket_error("socket receive failed");
  return result;
}

int oak_net_send_all(const struct oak_net_socket_t socket,
                     const void* data,
                     usize size)
{
  const char* cursor = data;
  while (size > 0)
  {
    const int n = send(native_socket(socket),
                       cursor,
                       (int)size,
#if defined(MSG_NOSIGNAL)
                       MSG_NOSIGNAL
#else
                       0
#endif
    );
    if (n <= 0)
    {
      log_socket_error("socket send failed");
      return 0;
    }
    cursor += n;
    size -= (usize)n;
  }
  return 1;
}
