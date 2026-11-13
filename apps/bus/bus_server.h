#ifndef IOTWIN_BUS_SERVER_H
#define IOTWIN_BUS_SERVER_H

/**
 * @file bus_server.h
 * @brief Virtual CAN bus over TCP.
 *
 * Every connected client (simulator, visualizer, test tool) is a node on
 * the bus. A frame received from one node is validated, normalised and
 * forwarded to all other nodes - like a broadcast on a physical CAN bus.
 * Invalid lines are counted and dropped. See docs/dev/protocol.md.
 *
 * The server is single threaded: call bus_server_poll() in a loop.
 * bus_server_port(), bus_server_client_count() and bus_server_get_stats()
 * may be called from other threads.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_SERVER_DEFAULT_PORT 8080
#define BUS_SERVER_MAX_CLIENTS 64

/** @brief Server configuration. */
typedef struct {
  const char* bind_address; /**< Interface to listen on (default "127.0.0.1"). */
  uint16_t port;            /**< TCP port, 0 = let the OS choose (default 8080). */
  size_t max_clients;       /**< Connection limit, 1..BUS_SERVER_MAX_CLIENTS (default 32). */
  bool echo;                /**< Also send frames back to their sender (default false). */
  bool log_frames;          /**< Log every routed frame at INFO level (default false). */
} BusServerConfig;

/** @brief Counters since start. */
typedef struct {
  uint64_t frames_routed;      /**< Valid frames received and forwarded. */
  uint64_t frames_rejected;    /**< Lines that were not valid frames. */
  uint64_t clients_accepted;   /**< Connections accepted in total. */
} BusServerStats;

typedef struct BusServer BusServer;

/** @brief Fills @p config with the defaults listed above. */
void bus_server_default_config(BusServerConfig* config);

/** @brief Creates the server and starts listening. Returns NULL on failure (logged). */
BusServer* bus_server_create(const BusServerConfig* config);

/** @brief Port the server listens on (useful with port 0). */
uint16_t bus_server_port(const BusServer* server);

/**
 * @brief Waits up to @p timeout_ms for network activity and processes it.
 * @return 0 on success, -1 on a fatal error.
 */
int bus_server_poll(BusServer* server, int timeout_ms);

/** @brief Number of currently connected clients. */
size_t bus_server_client_count(const BusServer* server);

/** @brief Snapshot of the counters. */
BusServerStats bus_server_get_stats(const BusServer* server);

/** @brief Disconnects all clients and frees the server. */
void bus_server_destroy(BusServer* server);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_BUS_SERVER_H */
