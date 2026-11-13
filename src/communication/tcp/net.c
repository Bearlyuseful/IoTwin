#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "net.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef WSAPOLLFD native_pollfd;
#define native_poll WSAPoll
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef struct pollfd native_pollfd;
#define native_poll poll
#endif

#ifdef MSG_NOSIGNAL
#define NET_SEND_FLAGS MSG_NOSIGNAL
#else
#define NET_SEND_FLAGS 0
#endif

static _Thread_local char g_error_text[160];

static int last_error_code(void) {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

static void remember_error(int code) {
#ifdef _WIN32
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                (DWORD)code, 0, g_error_text, sizeof(g_error_text), NULL);
  while (length > 0 && (g_error_text[length - 1] == '\n' || g_error_text[length - 1] == '\r' ||
                        g_error_text[length - 1] == ' ' || g_error_text[length - 1] == '.')) {
    g_error_text[--length] = '\0';
  }
  if (length == 0) {
    snprintf(g_error_text, sizeof(g_error_text), "socket error %d", code);
  }
#else
  snprintf(g_error_text, sizeof(g_error_text), "%s", strerror(code));
#endif
}

static bool is_would_block(int code) {
#ifdef _WIN32
  return code == WSAEWOULDBLOCK;
#else
  return code == EAGAIN || code == EWOULDBLOCK;
#endif
}

static bool is_interrupted(int code) {
#ifdef _WIN32
  return code == WSAEINTR;
#else
  return code == EINTR;
#endif
}

const char* net_last_error(void) { return g_error_text; }

bool net_init(void) {
#ifdef _WIN32
  WSADATA data;
  int rc = WSAStartup(MAKEWORD(2, 2), &data);
  if (rc != 0) {
    remember_error(rc);
    return false;
  }
#else
  signal(SIGPIPE, SIG_IGN);
#endif
  return true;
}

void net_cleanup(void) {
#ifdef _WIN32
  WSACleanup();
#endif
}

void net_close(net_socket_t socket) {
  if (socket == NET_INVALID_SOCKET) {
    return;
  }
#ifdef _WIN32
  closesocket((SOCKET)socket);
#else
  close(socket);
#endif
}

static void set_no_delay(net_socket_t socket) {
  int enable = 1;
  setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&enable, sizeof(enable));
}

net_socket_t net_connect_tcp(const char* host, uint16_t port) {
  char port_text[8];
  snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* results = NULL;
  int rc = getaddrinfo(host, port_text, &hints, &results);
  if (rc != 0) {
    snprintf(g_error_text, sizeof(g_error_text), "cannot resolve '%s': %s", host,
             gai_strerror(rc));
    return NET_INVALID_SOCKET;
  }

  net_socket_t result = NET_INVALID_SOCKET;
  for (struct addrinfo* it = results; it != NULL; it = it->ai_next) {
    net_socket_t s = (net_socket_t)socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (s == NET_INVALID_SOCKET) {
      remember_error(last_error_code());
      continue;
    }
    if (connect(s, it->ai_addr, (int)it->ai_addrlen) == 0) {
      set_no_delay(s);
      result = s;
      break;
    }
    remember_error(last_error_code());
    net_close(s);
  }
  freeaddrinfo(results);
  return result;
}

net_socket_t net_listen_tcp(const char* bind_address, uint16_t port, int backlog) {
  char port_text[8];
  snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* results = NULL;
  int rc = getaddrinfo(bind_address, port_text, &hints, &results);
  if (rc != 0) {
    snprintf(g_error_text, sizeof(g_error_text), "cannot resolve '%s': %s",
             bind_address ? bind_address : "*", gai_strerror(rc));
    return NET_INVALID_SOCKET;
  }

  net_socket_t result = NET_INVALID_SOCKET;
  for (struct addrinfo* it = results; it != NULL; it = it->ai_next) {
    net_socket_t s = (net_socket_t)socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (s == NET_INVALID_SOCKET) {
      remember_error(last_error_code());
      continue;
    }
    int enable = 1;
#ifdef _WIN32
    /* Prevents other processes from hijacking the port (SO_REUSEADDR means that on Windows). */
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&enable, sizeof(enable));
#else
    /* Allows an immediate restart while old connections are in TIME_WAIT. */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#endif
    if (bind(s, it->ai_addr, (int)it->ai_addrlen) == 0 && listen(s, backlog) == 0) {
      result = s;
      break;
    }
    remember_error(last_error_code());
    net_close(s);
  }
  freeaddrinfo(results);
  return result;
}

net_socket_t net_accept(net_socket_t listener, char* peer, size_t peer_size) {
  struct sockaddr_storage address;
  socklen_t length = sizeof(address);
  net_socket_t s = (net_socket_t)accept(listener, (struct sockaddr*)&address, &length);
  if (s == NET_INVALID_SOCKET) {
    remember_error(last_error_code());
    return NET_INVALID_SOCKET;
  }
  set_no_delay(s);
  if (peer != NULL && peer_size > 0) {
    char host[64];
    char service[16];
    if (getnameinfo((struct sockaddr*)&address, length, host, sizeof(host), service,
                    sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
      snprintf(peer, peer_size, "%s:%s", host, service);
    } else {
      snprintf(peer, peer_size, "unknown");
    }
  }
  return s;
}

uint16_t net_local_port(net_socket_t socket) {
  struct sockaddr_storage address;
  socklen_t length = sizeof(address);
  if (getsockname(socket, (struct sockaddr*)&address, &length) != 0) {
    remember_error(last_error_code());
    return 0;
  }
  if (address.ss_family == AF_INET) {
    return ntohs(((struct sockaddr_in*)&address)->sin_port);
  }
  if (address.ss_family == AF_INET6) {
    return ntohs(((struct sockaddr_in6*)&address)->sin6_port);
  }
  return 0;
}

bool net_set_nonblocking(net_socket_t socket, bool nonblocking) {
#ifdef _WIN32
  u_long mode = nonblocking ? 1 : 0;
  if (ioctlsocket((SOCKET)socket, FIONBIO, &mode) != 0) {
    remember_error(last_error_code());
    return false;
  }
  return true;
#else
  int flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) {
    remember_error(errno);
    return false;
  }
  flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  if (fcntl(socket, F_SETFL, flags) != 0) {
    remember_error(errno);
    return false;
  }
  return true;
#endif
}

int net_send_all(net_socket_t socket, const void* data, size_t length, int timeout_ms) {
  const char* bytes = (const char*)data;
  while (length > 0) {
    int chunk = length > 65536 ? 65536 : (int)length;
#ifdef _WIN32
    int sent = send((SOCKET)socket, bytes, chunk, 0);
#else
    ssize_t sent = send(socket, bytes, (size_t)chunk, NET_SEND_FLAGS);
#endif
    if (sent > 0) {
      bytes += sent;
      length -= (size_t)sent;
      continue;
    }
    int code = last_error_code();
    if (sent < 0 && is_interrupted(code)) {
      continue;
    }
    if (sent < 0 && is_would_block(code)) {
      NetPollFd entry = {socket, NET_POLL_OUT, 0};
      int ready = net_poll(&entry, 1, timeout_ms);
      if (ready > 0 && (entry.revents & NET_POLL_OUT)) {
        continue;
      }
      if (ready == 0) {
        snprintf(g_error_text, sizeof(g_error_text), "send timed out");
      }
      return -1;
    }
    remember_error(code);
    return -1;
  }
  return 0;
}

int net_recv(net_socket_t socket, void* buffer, size_t length) {
  int request = length > 65536 ? 65536 : (int)length;
  for (;;) {
#ifdef _WIN32
    int received = recv((SOCKET)socket, (char*)buffer, request, 0);
#else
    ssize_t received = recv(socket, buffer, (size_t)request, 0);
#endif
    if (received > 0) {
      return (int)received;
    }
    if (received == 0) {
      return NET_RECV_CLOSED;
    }
    int code = last_error_code();
    if (is_interrupted(code)) {
      continue;
    }
    if (is_would_block(code)) {
      return NET_RECV_WOULD_BLOCK;
    }
    remember_error(code);
    return NET_RECV_ERROR;
  }
}

int net_poll(NetPollFd* fds, size_t count, int timeout_ms) {
  if (count == 0 || count > NET_POLL_MAX) {
    snprintf(g_error_text, sizeof(g_error_text), "invalid poll set size %u", (unsigned)count);
    return -1;
  }
  native_pollfd native[NET_POLL_MAX];
  for (size_t i = 0; i < count; i++) {
    native[i].fd = fds[i].socket;
    native[i].events = 0;
    native[i].revents = 0;
    if (fds[i].events & NET_POLL_IN) {
      native[i].events |= POLLIN;
    }
    if (fds[i].events & NET_POLL_OUT) {
      native[i].events |= POLLOUT;
    }
    fds[i].revents = 0;
  }

  int rc;
  do {
#ifdef _WIN32
    rc = native_poll(native, (ULONG)count, timeout_ms);
#else
    rc = native_poll(native, (nfds_t)count, timeout_ms);
#endif
  } while (rc < 0 && is_interrupted(last_error_code()));

  if (rc < 0) {
    remember_error(last_error_code());
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    short r = native[i].revents;
    if (r & POLLIN) {
      fds[i].revents |= NET_POLL_IN;
    }
    if (r & POLLOUT) {
      fds[i].revents |= NET_POLL_OUT;
    }
    if (r & (POLLERR | POLLNVAL)) {
      fds[i].revents |= NET_POLL_ERR;
    }
    if (r & POLLHUP) {
      fds[i].revents |= NET_POLL_HUP;
    }
  }
  return rc;
}
