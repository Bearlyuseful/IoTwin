#ifndef IOTWIN_NET_H
#define IOTWIN_NET_H

/**
 * @file net.h
 * @brief Minimal portable TCP socket layer (POSIX sockets / Winsock2).
 *
 * Only what the bus server and the simulators need: connect, listen,
 * accept, send/recv, non-blocking mode and poll. On POSIX SIGPIPE is
 * ignored by net_init(), so writing to a closed peer returns an error
 * instead of killing the process.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
typedef uintptr_t net_socket_t; /**< Same representation as Winsock's SOCKET. */
#define NET_INVALID_SOCKET ((net_socket_t) ~(uintptr_t)0)
#else
typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#endif

/** @name Return codes of net_recv() */
/**@{*/
#define NET_RECV_CLOSED 0        /**< Peer closed the connection. */
#define NET_RECV_ERROR (-1)      /**< Socket error, connection unusable. */
#define NET_RECV_WOULD_BLOCK (-2) /**< Non-blocking socket has no data right now. */
/**@}*/

/** @name Poll event flags */
/**@{*/
#define NET_POLL_IN 0x1  /**< Readable (or a pending connection on a listener). */
#define NET_POLL_OUT 0x2 /**< Writable. */
#define NET_POLL_ERR 0x4 /**< Error condition (revents only). */
#define NET_POLL_HUP 0x8 /**< Peer hung up (revents only). */
/**@}*/

/** @brief Maximum number of descriptors accepted by net_poll(). */
#define NET_POLL_MAX 128

/** @brief Entry for net_poll(). */
typedef struct {
  net_socket_t socket; /**< Socket to watch. */
  int events;          /**< Requested NET_POLL_* flags. */
  int revents;         /**< Returned NET_POLL_* flags. */
} NetPollFd;

/** @brief Initialises the socket library. Safe to call multiple times (pair with net_cleanup()). */
bool net_init(void);

/** @brief Releases the socket library (Windows: WSACleanup). */
void net_cleanup(void);

/** @brief Opens a blocking TCP connection to host:port (TCP_NODELAY set). */
net_socket_t net_connect_tcp(const char* host, uint16_t port);

/**
 * @brief Creates a listening TCP socket.
 * @param bind_address Address to bind to, e.g. "127.0.0.1" or "0.0.0.0".
 * @param port Port to listen on, 0 lets the OS choose (see net_local_port()).
 */
net_socket_t net_listen_tcp(const char* bind_address, uint16_t port, int backlog);

/**
 * @brief Accepts a pending connection.
 * @param peer Optional buffer receiving "address:port" of the peer.
 */
net_socket_t net_accept(net_socket_t listener, char* peer, size_t peer_size);

/** @brief Returns the local port a socket is bound to (0 on error). */
uint16_t net_local_port(net_socket_t socket);

/** @brief Switches a socket between blocking and non-blocking mode. */
bool net_set_nonblocking(net_socket_t socket, bool nonblocking);

/**
 * @brief Sends all bytes, waiting up to @p timeout_ms whenever the send buffer is full.
 * @return 0 on success, -1 on error or timeout.
 */
int net_send_all(net_socket_t socket, const void* data, size_t length, int timeout_ms);

/**
 * @brief Receives up to @p length bytes.
 * @return Number of bytes (> 0) or NET_RECV_CLOSED / NET_RECV_ERROR / NET_RECV_WOULD_BLOCK.
 */
int net_recv(net_socket_t socket, void* buffer, size_t length);

/**
 * @brief Waits for socket events (poll() / WSAPoll()).
 * @param timeout_ms -1 waits forever, 0 returns immediately.
 * @return Number of sockets with events, 0 on timeout, -1 on error.
 */
int net_poll(NetPollFd* fds, size_t count, int timeout_ms);

/** @brief Closes a socket (ignores NET_INVALID_SOCKET). */
void net_close(net_socket_t socket);

/** @brief Text of the last socket error of the calling thread. */
const char* net_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_NET_H */
