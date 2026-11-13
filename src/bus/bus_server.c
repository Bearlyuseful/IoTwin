#include "bus_server.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_frame.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"

#define SEND_TIMEOUT_MS 200

typedef struct {
  net_socket_t socket;
  LineReader reader;
  char peer[64];
  unsigned id;
  bool closing;
} BusClient;

struct BusServer {
  BusServerConfig config;
  char bind_address[64];
  net_socket_t listener;
  uint16_t port;
  BusClient clients[BUS_SERVER_MAX_CLIENTS];
  size_t client_count;
  unsigned next_client_id;
  BusClient* current_sender; /* client whose input is being processed */

  atomic_size_t published_client_count;
  atomic_uint_fast64_t frames_routed;
  atomic_uint_fast64_t frames_rejected;
  atomic_uint_fast64_t clients_accepted;
};

void bus_server_default_config(BusServerConfig* config) {
  config->bind_address = "127.0.0.1";
  config->port = BUS_SERVER_DEFAULT_PORT;
  config->max_clients = 32;
  config->echo = false;
  config->log_frames = false;
}

BusServer* bus_server_create(const BusServerConfig* config) {
  if (config == NULL || config->max_clients == 0 || config->max_clients > BUS_SERVER_MAX_CLIENTS) {
    log_error("bus: invalid configuration (max clients must be 1..%d)", BUS_SERVER_MAX_CLIENTS);
    return NULL;
  }
  if (!net_init()) {
    log_error("bus: socket library initialisation failed: %s", net_last_error());
    return NULL;
  }

  BusServer* server = calloc(1, sizeof(BusServer));
  if (server == NULL) {
    net_cleanup();
    return NULL;
  }
  server->config = *config;
  snprintf(server->bind_address, sizeof(server->bind_address), "%s",
           config->bind_address ? config->bind_address : "127.0.0.1");
  server->config.bind_address = server->bind_address;
  server->next_client_id = 1;
  atomic_init(&server->published_client_count, 0);
  atomic_init(&server->frames_routed, 0);
  atomic_init(&server->frames_rejected, 0);
  atomic_init(&server->clients_accepted, 0);

  server->listener = net_listen_tcp(server->bind_address, config->port, 16);
  if (server->listener == NET_INVALID_SOCKET) {
    log_error("bus: cannot listen on %s:%u: %s", server->bind_address, (unsigned)config->port,
              net_last_error());
    free(server);
    net_cleanup();
    return NULL;
  }
  server->port = net_local_port(server->listener);
  log_info("bus: listening on %s:%u (max %u clients)", server->bind_address,
           (unsigned)server->port, (unsigned)config->max_clients);
  return server;
}

uint16_t bus_server_port(const BusServer* server) { return server->port; }

size_t bus_server_client_count(const BusServer* server) {
  return atomic_load(&((BusServer*)server)->published_client_count);
}

BusServerStats bus_server_get_stats(const BusServer* server) {
  BusServer* s = (BusServer*)server;
  BusServerStats stats;
  stats.frames_routed = atomic_load(&s->frames_routed);
  stats.frames_rejected = atomic_load(&s->frames_rejected);
  stats.clients_accepted = atomic_load(&s->clients_accepted);
  return stats;
}

static void accept_client(BusServer* server) {
  char peer[64] = "";
  net_socket_t socket = net_accept(server->listener, peer, sizeof(peer));
  if (socket == NET_INVALID_SOCKET) {
    log_warn("bus: accept failed: %s", net_last_error());
    return;
  }
  if (server->client_count >= server->config.max_clients) {
    log_warn("bus: rejecting %s - client limit of %u reached", peer,
             (unsigned)server->config.max_clients);
    net_close(socket);
    return;
  }
  BusClient* client = &server->clients[server->client_count++];
  memset(client, 0, sizeof(*client));
  client->socket = socket;
  client->id = server->next_client_id++;
  snprintf(client->peer, sizeof(client->peer), "%s", peer);
  line_reader_init(&client->reader);
  atomic_fetch_add(&server->clients_accepted, 1);
  log_info("bus: node #%u connected from %s (%u connected)", client->id, client->peer,
           (unsigned)server->client_count);
}

static void route_line(const char* line, size_t length, void* user_data) {
  BusServer* server = (BusServer*)user_data;
  BusClient* sender = server->current_sender;

  CanFrame frame;
  if (!can_frame_from_text(line, length, &frame)) {
    atomic_fetch_add(&server->frames_rejected, 1);
    log_warn("bus: node #%u sent an invalid frame: '%.*s'", sender->id,
             (int)(length > 40 ? 40 : length), line);
    return;
  }

  char text[CAN_FRAME_TEXT_MAX + 1];
  int text_length = can_frame_to_text(&frame, text, CAN_FRAME_TEXT_MAX);
  if (text_length < 0) {
    return;
  }
  text[text_length++] = '\n';

  for (size_t i = 0; i < server->client_count; i++) {
    BusClient* receiver = &server->clients[i];
    if (receiver->closing || (receiver == sender && !server->config.echo)) {
      continue;
    }
    if (net_send_all(receiver->socket, text, (size_t)text_length, SEND_TIMEOUT_MS) != 0) {
      log_warn("bus: sending to node #%u failed (%s), disconnecting", receiver->id,
               net_last_error());
      receiver->closing = true;
    }
  }
  atomic_fetch_add(&server->frames_routed, 1);
  if (server->config.log_frames) {
    log_info("bus: #%u %.*s", sender->id, text_length - 1, text);
  }
}

static void read_client(BusServer* server, BusClient* client) {
  char buffer[1024];
  int received = net_recv(client->socket, buffer, sizeof(buffer));
  if (received > 0) {
    server->current_sender = client;
    line_reader_feed(&client->reader, buffer, (size_t)received, route_line, server);
    server->current_sender = NULL;
    return;
  }
  if (received == NET_RECV_WOULD_BLOCK) {
    return;
  }
  if (received == NET_RECV_ERROR) {
    log_warn("bus: node #%u connection error: %s", client->id, net_last_error());
  }
  client->closing = true;
}

static void remove_closed_clients(BusServer* server) {
  size_t kept = 0;
  for (size_t i = 0; i < server->client_count; i++) {
    BusClient* client = &server->clients[i];
    if (client->closing) {
      net_close(client->socket);
      log_info("bus: node #%u (%s) disconnected (%u connected)", client->id, client->peer,
               (unsigned)(server->client_count - 1 - (i - kept)));
      continue;
    }
    if (kept != i) {
      server->clients[kept] = *client;
    }
    kept++;
  }
  server->client_count = kept;
}

int bus_server_poll(BusServer* server, int timeout_ms) {
  NetPollFd fds[BUS_SERVER_MAX_CLIENTS + 1];
  const size_t count = server->client_count;
  fds[0].socket = server->listener;
  fds[0].events = NET_POLL_IN;
  for (size_t i = 0; i < count; i++) {
    fds[i + 1].socket = server->clients[i].socket;
    fds[i + 1].events = NET_POLL_IN;
  }

  int ready = net_poll(fds, count + 1, timeout_ms);
  if (ready < 0) {
    log_error("bus: poll failed: %s", net_last_error());
    return -1;
  }
  if (ready > 0) {
    /* Existing clients first, so a newly accepted client does not shift the indices. */
    for (size_t i = 0; i < count; i++) {
      if (fds[i + 1].revents & (NET_POLL_IN | NET_POLL_HUP | NET_POLL_ERR)) {
        read_client(server, &server->clients[i]);
      }
    }
    remove_closed_clients(server);
    if (fds[0].revents & NET_POLL_IN) {
      accept_client(server);
    }
  }
  atomic_store(&server->published_client_count, server->client_count);
  return 0;
}

void bus_server_destroy(BusServer* server) {
  if (server == NULL) {
    return;
  }
  for (size_t i = 0; i < server->client_count; i++) {
    net_close(server->clients[i].socket);
  }
  net_close(server->listener);
  free(server);
  net_cleanup();
}
