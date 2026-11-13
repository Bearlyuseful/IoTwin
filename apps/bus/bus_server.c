#include "bus_server.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_frame.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"

#define SEND_TIMEOUT_MS 200
#define NAME_MAX 32
#define MAX_DELAY_MS 10000u
#define MAX_SILENCE_MS 600000u

typedef struct {
  net_socket_t socket;
  LineReader reader;
  char peer[64];
  char name[NAME_MAX];
  unsigned id;
  bool closing;
  bool testbench; /* receives test bench lines ("@...") */
} BusClient;

/* A frame waiting for its delivery time (fault "delay"). */
typedef struct {
  uint64_t due_ms;
  unsigned sender_id;
  int length;
  char text[CAN_FRAME_TEXT_MAX + 1];
} PendingFrame;

/* Fault model of the medium, changed with "!fault ..." commands. */
typedef struct {
  double drop_probability; /* 0..1 */
  bool drop_filtered;      /* only frames matching drop_id/drop_mask */
  uint32_t drop_id;
  uint32_t drop_mask;
  uint32_t delay_ms;
  uint32_t jitter_ms;
  uint64_t silence_until_ms;
  uint64_t random_state;
} BusFaults;

struct BusServer {
  BusServerConfig config;
  char bind_address[64];
  net_socket_t listener;
  uint16_t port;
  BusClient clients[BUS_SERVER_MAX_CLIENTS];
  size_t client_count;
  unsigned next_client_id;
  BusClient* current_sender; /* client whose input is being processed */

  BusFaults faults;
  PendingFrame pending[BUS_SERVER_MAX_PENDING];
  size_t pending_head;
  size_t pending_count;
  uint64_t last_due_ms; /* deliveries never overtake each other */

  atomic_size_t published_client_count;
  atomic_uint_fast64_t frames_routed;
  atomic_uint_fast64_t frames_rejected;
  atomic_uint_fast64_t frames_dropped;
  atomic_uint_fast64_t frames_delayed;
  atomic_uint_fast64_t clients_accepted;
};

void bus_server_default_config(BusServerConfig* config) {
  config->bind_address = "127.0.0.1";
  config->port = BUS_SERVER_DEFAULT_PORT;
  config->max_clients = 32;
  config->echo = false;
  config->log_frames = false;
}

static void clear_faults(BusFaults* faults) {
  const uint64_t seed = faults->random_state;
  memset(faults, 0, sizeof(*faults));
  faults->random_state = seed != 0 ? seed : 0x9E3779B97F4A7C15ull;
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
  clear_faults(&server->faults);
  atomic_init(&server->published_client_count, 0);
  atomic_init(&server->frames_routed, 0);
  atomic_init(&server->frames_rejected, 0);
  atomic_init(&server->frames_dropped, 0);
  atomic_init(&server->frames_delayed, 0);
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
  stats.frames_dropped = atomic_load(&s->frames_dropped);
  stats.frames_delayed = atomic_load(&s->frames_delayed);
  stats.clients_accepted = atomic_load(&s->clients_accepted);
  return stats;
}

/* "#3" or "#3 (ecu)" for log messages. */
static const char* client_label(const BusClient* client, char* buffer, size_t size) {
  if (client->name[0] != '\0') {
    snprintf(buffer, size, "#%u (%s)", client->id, client->name);
  } else {
    snprintf(buffer, size, "#%u", client->id);
  }
  return buffer;
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

static void send_to(BusClient* receiver, const char* text, size_t length) {
  if (receiver->closing) {
    return;
  }
  if (net_send_all(receiver->socket, text, length, SEND_TIMEOUT_MS) != 0) {
    log_warn("bus: sending to node #%u failed (%s), disconnecting", receiver->id, net_last_error());
    receiver->closing = true;
  }
}

static void reply(BusServer* server, BusClient* client, const char* format, ...)
    IOTWIN_PRINTF_FORMAT(3, 4);

static void reply(BusServer* server, BusClient* client, const char* format, ...) {
  (void)server;
  char text[256];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text, sizeof(text) - 1, format, args);
  va_end(args);
  if (length < 0) {
    return;
  }
  if ((size_t)length > sizeof(text) - 2) {
    length = (int)sizeof(text) - 2;
  }
  text[length++] = '\n';
  send_to(client, text, (size_t)length);
}

/* Sends a normalised frame line ("...\n") to every node except the sender. */
static void deliver(BusServer* server, unsigned sender_id, const char* text, size_t length) {
  for (size_t i = 0; i < server->client_count; i++) {
    BusClient* receiver = &server->clients[i];
    if (receiver->id == sender_id && !server->config.echo) {
      continue;
    }
    send_to(receiver, text, length);
  }
}

/* ------------------------------------------------------------------------- */
/* Fault model                                                                */
/* ------------------------------------------------------------------------- */

/* xorshift64*: reproducible with "!fault seed". */
static double random_unit(BusFaults* faults) {
  uint64_t x = faults->random_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  faults->random_state = x;
  return (double)((x * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0; /* 2^53 */
}

static bool should_drop(BusFaults* faults, const CanFrame* frame) {
  if (faults->drop_probability <= 0.0) {
    return false;
  }
  if (faults->drop_filtered && (frame->id & faults->drop_mask) != (faults->drop_id & faults->drop_mask)) {
    return false;
  }
  return faults->drop_probability >= 1.0 || random_unit(faults) < faults->drop_probability;
}

static void enqueue(BusServer* server, unsigned sender_id, const char* text, int length, uint64_t now_ms) {
  uint64_t due = now_ms + server->faults.delay_ms;
  if (server->faults.jitter_ms > 0) {
    due += (uint64_t)(random_unit(&server->faults) * (double)(server->faults.jitter_ms + 1u));
  }
  if (due < server->last_due_ms) {
    due = server->last_due_ms; /* CAN does not reorder frames */
  }
  if (server->pending_count == BUS_SERVER_MAX_PENDING) {
    log_warn("bus: delay queue full, delivering immediately");
    deliver(server, sender_id, text, (size_t)length);
    return;
  }
  PendingFrame* pending = &server->pending[(server->pending_head + server->pending_count) % BUS_SERVER_MAX_PENDING];
  pending->due_ms = due;
  pending->sender_id = sender_id;
  pending->length = length;
  memcpy(pending->text, text, (size_t)length);
  server->pending_count++;
  server->last_due_ms = due;
  atomic_fetch_add(&server->frames_delayed, 1);
}

static void deliver_due(BusServer* server, uint64_t now_ms) {
  while (server->pending_count > 0) {
    PendingFrame* pending = &server->pending[server->pending_head];
    if (pending->due_ms > now_ms) {
      return;
    }
    deliver(server, pending->sender_id, pending->text, (size_t)pending->length);
    server->pending_head = (server->pending_head + 1u) % BUS_SERVER_MAX_PENDING;
    server->pending_count--;
  }
}

/* ------------------------------------------------------------------------- */
/* Control commands ("!...") and test bench lines ("@...")                     */
/* ------------------------------------------------------------------------- */

static bool parse_uint(const char* text, unsigned long max, unsigned long* value, int base) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  char* end = NULL;
  const unsigned long parsed = strtoul(text, &end, base);
  if (end == text || *end != '\0' || parsed > max) {
    return false;
  }
  *value = parsed;
  return true;
}

static void fault_command(BusServer* server, BusClient* client, char** argv, int argc) {
  BusFaults* faults = &server->faults;
  const uint64_t now = pal_time_monotonic_ms();
  char label[64];
  client_label(client, label, sizeof(label));
  unsigned long a = 0;
  unsigned long b = 0;

  if (argc >= 2 && strcmp(argv[1], "clear") == 0 && argc == 2) {
    clear_faults(faults);
    log_info("bus: faults cleared by node %s", label);
    reply(server, client, "!ok");
  } else if (argc >= 3 && strcmp(argv[1], "drop") == 0 && argc <= 4) {
    char* end = NULL;
    const double percent = strtod(argv[2], &end);
    if (end == argv[2] || *end != '\0' || percent < 0.0 || percent > 100.0) {
      reply(server, client, "!error drop: percent 0..100");
      return;
    }
    faults->drop_filtered = false;
    if (argc == 4) {
      char* slash = strchr(argv[3], '/');
      if (slash != NULL) {
        *slash = '\0';
      }
      if (!parse_uint(argv[3], CAN_EFF_MASK, &a, 16) ||
          (slash != NULL && !parse_uint(slash + 1, CAN_EFF_MASK, &b, 16))) {
        reply(server, client, "!error drop: id[/mask] in hex");
        return;
      }
      faults->drop_filtered = true;
      faults->drop_id = (uint32_t)a;
      faults->drop_mask = slash != NULL ? (uint32_t)b : CAN_EFF_MASK;
    }
    faults->drop_probability = percent / 100.0;
    log_info("bus: fault by node %s: drop %.1f %% of %s", label, percent,
             faults->drop_filtered ? "the matching frames" : "all frames");
    reply(server, client, "!ok");
  } else if (argc >= 3 && strcmp(argv[1], "delay") == 0 && argc <= 4) {
    if (!parse_uint(argv[2], MAX_DELAY_MS, &a, 10) || (argc == 4 && !parse_uint(argv[3], MAX_DELAY_MS, &b, 10))) {
      reply(server, client, "!error delay: ms [jitter ms], each 0..%u", MAX_DELAY_MS);
      return;
    }
    faults->delay_ms = (uint32_t)a;
    faults->jitter_ms = argc == 4 ? (uint32_t)b : 0u;
    log_info("bus: fault by node %s: delay %lu ms, jitter %lu ms", label, a, argc == 4 ? b : 0ul);
    reply(server, client, "!ok");
  } else if (argc == 3 && strcmp(argv[1], "silence") == 0) {
    if (!parse_uint(argv[2], MAX_SILENCE_MS, &a, 10)) {
      reply(server, client, "!error silence: ms 0..%u", MAX_SILENCE_MS);
      return;
    }
    faults->silence_until_ms = now + a;
    log_info("bus: fault by node %s: bus silent for %lu ms", label, a);
    reply(server, client, "!ok");
  } else if (argc == 3 && strcmp(argv[1], "seed") == 0) {
    if (!parse_uint(argv[2], 0xFFFFFFFFul, &a, 10)) {
      reply(server, client, "!error seed: number");
      return;
    }
    faults->random_state = (uint64_t)a * 0x9E3779B97F4A7C15ull + 1u;
    reply(server, client, "!ok");
  } else {
    reply(server, client, "!error usage: fault drop P [ID[/MASK]] | delay MS [JITTER] | silence MS | seed N | clear");
  }
}

static void control_command(BusServer* server, BusClient* client, const char* line, size_t length) {
  char text[LINE_READER_MAX_LINE];
  if (length >= sizeof(text)) {
    length = sizeof(text) - 1;
  }
  memcpy(text, line, length);
  text[length] = '\0';

  char* argv[8];
  int argc = 0;
  for (char* token = strtok(text + 1, " \t"); token != NULL && argc < 8; token = strtok(NULL, " \t")) {
    argv[argc++] = token;
  }
  if (argc == 0) {
    reply(server, client, "!error empty command");
    return;
  }

  if (strcmp(argv[0], "name") == 0 && argc == 2) {
    snprintf(client->name, sizeof(client->name), "%s", argv[1]);
    log_info("bus: node #%u is '%s'", client->id, client->name);
    reply(server, client, "!ok");
  } else if (strcmp(argv[0], "testbench") == 0 && argc == 2 &&
             (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
    client->testbench = strcmp(argv[1], "on") == 0;
    reply(server, client, "!ok");
  } else if (strcmp(argv[0], "fault") == 0) {
    fault_command(server, client, argv, argc);
  } else if (strcmp(argv[0], "status") == 0 && argc == 1) {
    const BusFaults* f = &server->faults;
    const uint64_t now = pal_time_monotonic_ms();
    reply(server, client,
          "!status nodes=%u routed=%llu dropped=%llu delayed=%llu drop=%.1f%%%s delay=%u jitter=%u silence=%llu",
          (unsigned)server->client_count, (unsigned long long)atomic_load(&server->frames_routed),
          (unsigned long long)atomic_load(&server->frames_dropped),
          (unsigned long long)atomic_load(&server->frames_delayed), f->drop_probability * 100.0,
          f->drop_filtered ? "(filtered)" : "", (unsigned)f->delay_ms, (unsigned)f->jitter_ms,
          (unsigned long long)(f->silence_until_ms > now ? f->silence_until_ms - now : 0u));
  } else {
    reply(server, client, "!error unknown command '%s' (name, testbench, fault, status)", argv[0]);
  }
}

/* "@<target> <command>": for test bench nodes (e.g. can_sim), never CAN traffic. */
static void testbench_line(BusServer* server, BusClient* sender, const char* line, size_t length) {
  char text[LINE_READER_MAX_LINE + 1];
  if (length >= sizeof(text) - 1) {
    length = sizeof(text) - 2;
  }
  memcpy(text, line, length);
  text[length++] = '\n';
  for (size_t i = 0; i < server->client_count; i++) {
    BusClient* receiver = &server->clients[i];
    if (receiver != sender && receiver->testbench) {
      send_to(receiver, text, length);
    }
  }
  if (server->config.log_frames) {
    char label[64];
    log_info("bus: %s %.*s", client_label(sender, label, sizeof(label)), (int)length - 1, text);
  }
}

/* ------------------------------------------------------------------------- */
/* Routing                                                                    */
/* ------------------------------------------------------------------------- */

static void route_line(const char* line, size_t length, void* user_data) {
  BusServer* server = (BusServer*)user_data;
  BusClient* sender = server->current_sender;

  if (length > 0 && line[0] == '!') {
    control_command(server, sender, line, length);
    return;
  }
  if (length > 0 && line[0] == '@') {
    testbench_line(server, sender, line, length);
    return;
  }

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
  atomic_fetch_add(&server->frames_routed, 1);
  if (server->config.log_frames) {
    char label[64];
    log_info("bus: %s %.*s", client_label(sender, label, sizeof(label)), text_length - 1, text);
  }

  const uint64_t now = pal_time_monotonic_ms();
  if (now < server->faults.silence_until_ms || should_drop(&server->faults, &frame)) {
    atomic_fetch_add(&server->frames_dropped, 1);
    return;
  }
  if (server->faults.delay_ms > 0 || server->faults.jitter_ms > 0 || server->pending_count > 0) {
    enqueue(server, sender->id, text, text_length, now);
    return;
  }
  deliver(server, sender->id, text, (size_t)text_length);
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
      char label[64];
      net_close(client->socket);
      log_info("bus: node %s (%s) disconnected (%u connected)", client_label(client, label, sizeof(label)),
               client->peer, (unsigned)(server->client_count - 1 - (i - kept)));
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

  /* Wake up in time for delayed frames. */
  if (server->pending_count > 0) {
    const uint64_t now = pal_time_monotonic_ms();
    const uint64_t due = server->pending[server->pending_head].due_ms;
    const int until_due = due > now ? (int)(due - now) : 0;
    if (timeout_ms < 0 || until_due < timeout_ms) {
      timeout_ms = until_due;
    }
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
  }
  deliver_due(server, pal_time_monotonic_ms());
  remove_closed_clients(server);
  if (ready > 0 && (fds[0].revents & NET_POLL_IN)) {
    accept_client(server);
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
