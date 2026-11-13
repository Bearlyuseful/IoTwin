/**
 * @file main_uds_client.c
 * @brief uds_client: UDS tester (ISO 14229 over ISO-TP) for nodes on iotwin-bus.
 *
 * One-shot:     uds_client scan
 *               uds_client --target gateway read 0200 0201
 *               uds_client session extended + write 0103 012C
 * Interactive:  uds_client          (commands from stdin; TesterPresent keeps
 *                                    a non-default session alive)
 *
 * Single threaded except for the stdin reader in interactive mode: all bus
 * traffic is handled by the main thread.
 */

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_frame.h"
#include "cli.h"
#include "isotp.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"
#include "uds.h"

#define DEFAULT_TIMEOUT_MS 1000
#define TESTER_PRESENT_INTERVAL_MS 2000
#define SEND_TIMEOUT_MS 200
#define MAX_TOKENS 32

typedef struct {
  net_socket_t socket;
  LineReader reader;
  IsoTpLink link;
  uint8_t rx_buffer[ISOTP_MAX_LENGTH];
  uint32_t timeout_ms;
  bool connection_lost;
  uint8_t session;
  uint64_t last_request_ms;
} Client;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static uint64_t now_us(void) { return pal_time_monotonic_ns() / 1000u; }

/* ------------------------------------------------------------------------- */
/* Knowledge about identifiers and trouble codes                              */
/* ------------------------------------------------------------------------- */

typedef enum { FORMAT_TEXT, FORMAT_UNSIGNED, FORMAT_SESSION, FORMAT_HEX, FORMAT_BYTES } ValueFormat;

typedef struct {
  uint16_t did;
  const char* name;
  ValueFormat format;
  const char* unit;
} DidInfo;

static const DidInfo kDids[] = {
    {UDS_DID_ACTIVE_SESSION, "active diagnostic session", FORMAT_SESSION, ""},
    {UDS_DID_SPARE_PART_NUMBER, "spare part number", FORMAT_TEXT, ""},
    {UDS_DID_SOFTWARE_VERSION, "software version", FORMAT_TEXT, ""},
    {UDS_DID_SERIAL_NUMBER, "serial number", FORMAT_TEXT, ""}, /* hex if not printable */
    {UDS_DID_VIN, "VIN", FORMAT_TEXT, ""},
    /* light ECU (can_sim) */
    {0x0100, "lamp output", FORMAT_UNSIGNED, ""},
    {0x0101, "switch input", FORMAT_UNSIGNED, ""},
    {0x0102, "LAMP_CMD cycle time", FORMAT_UNSIGNED, "ms"},
    {0x0103, "blink period", FORMAT_UNSIGNED, "ms"},
    {0x0104, "supply voltage", FORMAT_UNSIGNED, "mV"},
    {0x0105, "brightness", FORMAT_UNSIGNED, "%"},
    {0x0106, "lamp current", FORMAT_UNSIGNED, "mA"},
    {0x0107, "fuse blown, heat", FORMAT_BYTES, "%"},
    /* gateway (firmware) */
    {0x0200, "CAN error counters TEC REC, state", FORMAT_HEX, ""},
    {0x0201, "frames to CAN, from CAN, echoes", FORMAT_HEX, ""},
    {0x0202, "uptime", FORMAT_UNSIGNED, "ms"},
    {0x0203, "CAN channel open, mode, bitrate", FORMAT_HEX, ""},
    {0x0204, "LED brightness set, measured", FORMAT_BYTES, "%"},
};

static const DidInfo* find_did(uint16_t did) {
  for (size_t i = 0; i < sizeof(kDids) / sizeof(kDids[0]); i++) {
    if (kDids[i].did == did) {
      return &kDids[i];
    }
  }
  return NULL;
}

typedef struct {
  uint32_t code;
  const char* text;
} DtcInfo;

static const DtcInfo kDtcs[] = {
    {0x900111, "lamp output short to ground"},
    {0x900113, "lamp output open circuit"},
    {0x900216, "supply voltage low"},
    {0xC07388, "CAN bus off"},
};

static const char* dtc_text(uint32_t code) {
  for (size_t i = 0; i < sizeof(kDtcs) / sizeof(kDtcs[0]); i++) {
    if (kDtcs[i].code == code) {
      return kDtcs[i].text;
    }
  }
  return "";
}

static void print_status_bits(uint8_t status) {
  static const char* const kBits[8] = {"failed", "failed this cycle", "pending", "confirmed",
                                       "not tested since clear", "failed since clear",
                                       "not tested this cycle", "warning indicator"};
  if (status == 0) {
    printf("tested, passed");
    return;
  }
  bool first = true;
  for (int bit = 0; bit < 8; bit++) {
    if (status & (1u << bit)) {
      printf("%s%s", first ? "" : ", ", kBits[bit]);
      first = false;
    }
  }
}

static void print_value(const DidInfo* info, const uint8_t* data, size_t length) {
  bool printable = length > 0;
  for (size_t i = 0; i < length; i++) {
    printable = printable && data[i] >= 0x20 && data[i] < 0x7F;
  }
  const ValueFormat format = info != NULL ? info->format : (printable ? FORMAT_TEXT : FORMAT_HEX);
  if (format == FORMAT_TEXT && printable) {
    printf("\"%.*s\"", (int)length, (const char*)data);
  } else if (format == FORMAT_SESSION && length == 1) {
    printf("%s (0x%02X)", uds_session_name(data[0]), data[0]);
  } else if (format == FORMAT_BYTES) {
    for (size_t i = 0; i < length; i++) {
      printf("%s%u", i ? " / " : "", data[i]);
    }
    printf(" %s", info->unit);
  } else if (format == FORMAT_UNSIGNED && length >= 1 && length <= 4) {
    uint32_t value = 0;
    for (size_t i = 0; i < length; i++) {
      value = (value << 8) | data[i];
    }
    printf("%lu%s%s", (unsigned long)value, info->unit[0] ? " " : "", info->unit);
  } else {
    for (size_t i = 0; i < length; i++) {
      printf("%s%02X", i ? " " : "", data[i]);
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Transport                                                                  */
/* ------------------------------------------------------------------------- */

static bool link_send(void* context, const CanFrame* frame) {
  Client* client = (Client*)context;
  char text[CAN_FRAME_TEXT_MAX + 1];
  int length = can_frame_to_text(frame, text, CAN_FRAME_TEXT_MAX);
  if (length < 0) {
    return false;
  }
  log_debug("TX %s", text);
  text[length++] = '\n';
  if (net_send_all(client->socket, text, (size_t)length, SEND_TIMEOUT_MS) != 0) {
    client->connection_lost = true;
    return false;
  }
  return true;
}

static void on_line(const char* line, size_t length, void* user_data) {
  Client* client = (Client*)user_data;
  CanFrame frame;
  if (can_frame_from_text(line, length, &frame)) {
    if (frame.id == client->link.config.rx_id) {
      log_debug("RX %s", line);
    }
    isotp_on_frame(&client->link, &frame, now_us());
  }
}

/* Receives for up to timeout_ms. Returns false if the connection is gone. */
static bool pump(Client* client, int timeout_ms) {
  NetPollFd entry = {client->socket, NET_POLL_IN, 0};
  if (net_poll(&entry, 1, timeout_ms) > 0) {
    char buffer[2048];
    for (;;) {
      int received = net_recv(client->socket, buffer, sizeof(buffer));
      if (received > 0) {
        line_reader_feed(&client->reader, buffer, (size_t)received, on_line, client);
        continue;
      }
      if (received != NET_RECV_WOULD_BLOCK) {
        client->connection_lost = true;
      }
      break;
    }
  }
  isotp_poll(&client->link, now_us());
  return !client->connection_lost;
}

/**
 * Sends a request and waits for the response (NRC 0x78 extends the wait).
 * @return Response length, 0 if no response was expected, -1 on error.
 */
static int transact(Client* client, const uint8_t* request, size_t length, bool expect_response) {
  size_t stale = 0;
  pump(client, 0);
  while (isotp_receive(&client->link, &stale)) {
  }
  (void)isotp_take_rx_error(&client->link);

  const IsoTpResult result = isotp_send(&client->link, request, length, now_us());
  if (result != ISOTP_OK) {
    log_error("sending the request failed: %s", isotp_result_name(result));
    return -1;
  }
  client->last_request_ms = pal_time_monotonic_ms();
  uint64_t deadline_ms = client->last_request_ms + client->timeout_ms;

  while (!g_stop) {
    if (!pump(client, 5)) {
      log_error("connection to the bus lost");
      return -1;
    }
    if (!isotp_tx_busy(&client->link) && isotp_tx_result(&client->link) != ISOTP_OK) {
      log_error("sending the request failed: %s", isotp_result_name(isotp_tx_result(&client->link)));
      return -1;
    }
    size_t received = 0;
    if (isotp_receive(&client->link, &received)) {
      if (received >= 3 && client->rx_buffer[0] == UDS_SID_NEGATIVE_RESPONSE &&
          client->rx_buffer[2] == UDS_NRC_RESPONSE_PENDING) {
        deadline_ms = pal_time_monotonic_ms() + UDS_DEFAULT_P2_STAR_MS;
        continue;
      }
      return (int)received;
    }
    const IsoTpResult rx_error = isotp_take_rx_error(&client->link);
    if (rx_error != ISOTP_OK) {
      log_error("receiving the response failed: %s", isotp_result_name(rx_error));
      return -1;
    }
    if (!expect_response && !isotp_tx_busy(&client->link)) {
      return 0;
    }
    if (pal_time_monotonic_ms() > deadline_ms) {
      if (expect_response) {
        log_error("no response from 0x%03lX within %lu ms (is the node running?)",
                  (unsigned long)client->link.config.rx_id, (unsigned long)client->timeout_ms);
        return -1;
      }
      return 0;
    }
  }
  return -1;
}

/* Sends and checks for a positive response. Returns the response length or -1. */
static int request_positive(Client* client, const uint8_t* request, size_t length) {
  int received = transact(client, request, length, true);
  if (received < 0) {
    return -1;
  }
  const uint8_t* response = client->rx_buffer;
  if (received >= 3 && response[0] == UDS_SID_NEGATIVE_RESPONSE) {
    printf("negative response to %s: %s (0x%02X)\n", uds_service_name(response[1]), uds_nrc_name(response[2]),
           response[2]);
    return -1;
  }
  if (received < 1 || response[0] != (uint8_t)(request[0] + UDS_POSITIVE_RESPONSE_OFFSET)) {
    printf("unexpected response (%d bytes, first 0x%02X)\n", received, received > 0 ? response[0] : 0);
    return -1;
  }
  return received;
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                   */
/* ------------------------------------------------------------------------- */

static bool parse_hex_value(const char* text, size_t digits, uint32_t* value) {
  if (strlen(text) != digits) {
    return false;
  }
  char* end = NULL;
  const unsigned long parsed = strtoul(text, &end, 16);
  if (end == NULL || *end != '\0') {
    return false;
  }
  *value = (uint32_t)parsed;
  return true;
}

/* Hex bytes from one or more tokens: "2E0103012C" or "2E 01 03 01 2C". */
static bool parse_hex_bytes(int argc, char** argv, uint8_t* out, size_t capacity, size_t* length) {
  *length = 0;
  for (int i = 0; i < argc; i++) {
    const char* text = argv[i];
    const size_t digits = strlen(text);
    if (digits % 2 != 0) {
      return false;
    }
    for (size_t j = 0; j < digits; j += 2) {
      char pair[3] = {text[j], text[j + 1], '\0'};
      uint32_t value = 0;
      if (*length >= capacity || !parse_hex_value(pair, 2, &value)) {
        return false;
      }
      out[(*length)++] = (uint8_t)value;
    }
  }
  return *length > 0;
}

static int read_one(Client* client, uint16_t did, bool quiet_if_unsupported) {
  const uint8_t request[] = {UDS_SID_READ_DATA_BY_IDENTIFIER, (uint8_t)(did >> 8), (uint8_t)did};
  int received = transact(client, request, sizeof(request), true);
  if (received < 0) {
    return 1;
  }
  const uint8_t* response = client->rx_buffer;
  const DidInfo* info = find_did(did);
  if (received >= 3 && response[0] == UDS_SID_NEGATIVE_RESPONSE) {
    if (quiet_if_unsupported && response[2] == UDS_NRC_REQUEST_OUT_OF_RANGE) {
      return 0;
    }
    printf("%04X %-36s negative response: %s (0x%02X)\n", did, info ? info->name : "", uds_nrc_name(response[2]),
           response[2]);
    return 1;
  }
  if (received < 3 || response[0] != 0x62 || response[1] != (uint8_t)(did >> 8) || response[2] != (uint8_t)did) {
    printf("%04X unexpected response\n", did);
    return 1;
  }
  printf("%04X %-36s ", did, info ? info->name : "");
  print_value(info, &response[3], (size_t)received - 3u);
  printf("\n");
  return 0;
}

static int list_dtcs(Client* client, uint8_t report, uint8_t mask) {
  uint8_t request[3] = {UDS_SID_READ_DTC_INFORMATION, report, mask};
  int received = request_positive(client, request, report == 0x0A ? 2u : 3u);
  if (received < 3) {
    return 1;
  }
  const uint8_t* response = client->rx_buffer;
  const size_t count = ((size_t)received - 3u) / 4u;
  if (count == 0) {
    printf("no DTCs (status mask 0x%02X)\n", mask);
  }
  for (size_t i = 0; i < count; i++) {
    const uint8_t* entry = &response[3 + i * 4];
    const uint32_t code = ((uint32_t)entry[0] << 16) | ((uint32_t)entry[1] << 8) | entry[2];
    char text[16];
    uds_format_dtc(code, text, sizeof(text));
    printf("%s  %-30s status %02X: ", text, dtc_text(code), entry[3]);
    print_status_bits(entry[3]);
    printf("\n");
  }
  return 0;
}

static void print_command_help(void) {
  printf("Commands (several in one call: separate them with '+'):\n"
         "  scan                      read the identification (F187 F189 F18C F190 F186)\n"
         "  read <DID> [<DID> ...]    ReadDataByIdentifier, e.g. read F190 0100\n"
         "  write <DID> <hex>         WriteDataByIdentifier (extended session), e.g. write 0103 012C\n"
         "  session default|extended  DiagnosticSessionControl\n"
         "  tester-present            TesterPresent\n"
         "  reset hard|soft           ECUReset\n"
         "  dtc [all|<mask>]          DTCs matching a status mask (default 09 = failed or confirmed)\n"
         "  dtc-supported             all DTCs the node knows, with status\n"
         "  clear [<DTC>]             ClearDiagnosticInformation, all or one (e.g. B1001-11)\n"
         "  raw <hex>                 any request, e.g. raw 22F190\n"
         "  help | quit\n");
}

static int run_command(Client* client, int argc, char** argv) {
  const char* command = argv[0];
  if (strcmp(command, "help") == 0) {
    print_command_help();
    return 0;
  }
  if (strcmp(command, "scan") == 0) {
    const uint16_t dids[] = {UDS_DID_SPARE_PART_NUMBER, UDS_DID_SOFTWARE_VERSION, UDS_DID_SERIAL_NUMBER, UDS_DID_VIN,
                             UDS_DID_ACTIVE_SESSION};
    int failures = 0;
    for (size_t i = 0; i < sizeof(dids) / sizeof(dids[0]) && !g_stop; i++) {
      failures += read_one(client, dids[i], true);
      if (client->connection_lost || failures > 0) {
        break;
      }
    }
    return failures > 0 ? 1 : 0;
  }
  if (strcmp(command, "read") == 0 && argc >= 2) {
    int failures = 0;
    for (int i = 1; i < argc; i++) {
      uint32_t did = 0;
      if (!parse_hex_value(argv[i], 4, &did)) {
        printf("invalid identifier '%s' (4 hex digits, e.g. F190)\n", argv[i]);
        return 2;
      }
      failures += read_one(client, (uint16_t)did, false);
    }
    return failures > 0 ? 1 : 0;
  }
  if (strcmp(command, "write") == 0 && argc >= 3) {
    uint32_t did = 0;
    uint8_t request[UDS_MAX_REQUEST];
    size_t data_length = 0;
    if (!parse_hex_value(argv[1], 4, &did) ||
        !parse_hex_bytes(argc - 2, &argv[2], &request[3], sizeof(request) - 3u, &data_length)) {
      printf("usage: write <DID> <hex bytes>, e.g. write 0103 012C\n");
      return 2;
    }
    request[0] = UDS_SID_WRITE_DATA_BY_IDENTIFIER;
    request[1] = (uint8_t)(did >> 8);
    request[2] = (uint8_t)did;
    if (request_positive(client, request, data_length + 3u) < 0) {
      return 1;
    }
    printf("%04lX written\n", (unsigned long)did);
    return 0;
  }
  if (strcmp(command, "session") == 0 && argc == 2) {
    uint8_t session = 0;
    if (strcmp(argv[1], "default") == 0) {
      session = UDS_SESSION_DEFAULT;
    } else if (strcmp(argv[1], "extended") == 0) {
      session = UDS_SESSION_EXTENDED;
    } else if (strcmp(argv[1], "programming") == 0) {
      session = UDS_SESSION_PROGRAMMING;
    } else {
      printf("usage: session default|extended\n");
      return 2;
    }
    const uint8_t request[] = {UDS_SID_DIAGNOSTIC_SESSION_CONTROL, session};
    int received = request_positive(client, request, sizeof(request));
    if (received < 0) {
      return 1;
    }
    client->session = session;
    if (received >= 6) {
      const uint8_t* r = client->rx_buffer;
      printf("session %s (P2 %u ms, P2* %u ms)\n", uds_session_name(session), (unsigned)((r[2] << 8) | r[3]),
             (unsigned)(((r[4] << 8) | r[5]) * 10));
    } else {
      printf("session %s\n", uds_session_name(session));
    }
    return 0;
  }
  if (strcmp(command, "tester-present") == 0) {
    const uint8_t request[] = {UDS_SID_TESTER_PRESENT, 0x00};
    if (request_positive(client, request, sizeof(request)) < 0) {
      return 1;
    }
    printf("tester present acknowledged\n");
    return 0;
  }
  if (strcmp(command, "reset") == 0 && argc == 2) {
    uint8_t type = 0;
    if (strcmp(argv[1], "hard") == 0) {
      type = UDS_RESET_HARD;
    } else if (strcmp(argv[1], "soft") == 0) {
      type = UDS_RESET_SOFT;
    } else {
      printf("usage: reset hard|soft\n");
      return 2;
    }
    const uint8_t request[] = {UDS_SID_ECU_RESET, type};
    if (request_positive(client, request, sizeof(request)) < 0) {
      return 1;
    }
    client->session = UDS_SESSION_DEFAULT;
    printf("%s reset accepted\n", argv[1]);
    return 0;
  }
  if (strcmp(command, "dtc") == 0 && argc <= 2) {
    uint32_t mask = UDS_DTC_TEST_FAILED | UDS_DTC_CONFIRMED;
    if (argc == 2) {
      if (strcmp(argv[1], "all") == 0) {
        mask = 0xFF;
      } else if (!parse_hex_value(argv[1], 2, &mask)) {
        printf("usage: dtc [all|<status mask, 2 hex digits>]\n");
        return 2;
      }
    }
    return list_dtcs(client, 0x02, (uint8_t)mask);
  }
  if (strcmp(command, "dtc-supported") == 0) {
    return list_dtcs(client, 0x0A, 0xFF);
  }
  if (strcmp(command, "clear") == 0 && argc <= 2) {
    uint32_t group = 0xFFFFFF;
    if (argc == 2 && strcmp(argv[1], "all") != 0 && !uds_parse_dtc(argv[1], &group)) {
      printf("usage: clear [all|<DTC>], e.g. clear B1001-11\n");
      return 2;
    }
    const uint8_t request[] = {UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION, (uint8_t)(group >> 16), (uint8_t)(group >> 8),
                               (uint8_t)group};
    if (request_positive(client, request, sizeof(request)) < 0) {
      return 1;
    }
    printf("DTCs cleared\n");
    return 0;
  }
  if (strcmp(command, "raw") == 0 && argc >= 2) {
    uint8_t request[UDS_MAX_REQUEST];
    size_t length = 0;
    if (!parse_hex_bytes(argc - 1, &argv[1], request, sizeof(request), &length)) {
      printf("usage: raw <hex bytes>, e.g. raw 22F190\n");
      return 2;
    }
    const bool suppressed = length >= 2 && (request[0] == UDS_SID_TESTER_PRESENT ||
                                            request[0] == UDS_SID_DIAGNOSTIC_SESSION_CONTROL ||
                                            request[0] == UDS_SID_ECU_RESET) &&
                            (request[1] & UDS_SUPPRESS_POSITIVE_RESPONSE);
    int received = transact(client, request, length, !suppressed);
    if (received < 0) {
      return 1;
    }
    if (received == 0) {
      printf("(no response expected)\n");
      return 0;
    }
    printf("%s:", uds_service_name(client->rx_buffer[0]));
    for (int i = 0; i < received; i++) {
      printf(" %02X", client->rx_buffer[i]);
    }
    if (received >= 3 && client->rx_buffer[0] == UDS_SID_NEGATIVE_RESPONSE) {
      printf("  (%s)", uds_nrc_name(client->rx_buffer[2]));
    }
    printf("\n");
    return client->rx_buffer[0] == UDS_SID_NEGATIVE_RESPONSE ? 1 : 0;
  }
  printf("unknown or incomplete command '%s' (type 'help')\n", command);
  return 2;
}

/* ------------------------------------------------------------------------- */
/* Interactive mode                                                           */
/* ------------------------------------------------------------------------- */

#define INPUT_SLOTS 8

typedef struct {
  pthread_mutex_t lock;
  char lines[INPUT_SLOTS][256];
  size_t head;
  size_t count;
  bool end_of_input;
} InputQueue;

static void* input_thread(void* argument) {
  InputQueue* queue = (InputQueue*)argument;
  char line[256];
  while (!g_stop && fgets(line, sizeof(line), stdin) != NULL) {
    pthread_mutex_lock(&queue->lock);
    if (queue->count < INPUT_SLOTS) {
      memcpy(queue->lines[(queue->head + queue->count) % INPUT_SLOTS], line, sizeof(line));
      queue->count++;
    }
    pthread_mutex_unlock(&queue->lock);
  }
  pthread_mutex_lock(&queue->lock);
  queue->end_of_input = true;
  pthread_mutex_unlock(&queue->lock);
  return NULL;
}

static int tokenize(char* line, char** tokens) {
  int count = 0;
  for (char* token = strtok(line, " \t\r\n"); token != NULL && count < MAX_TOKENS; token = strtok(NULL, " \t\r\n")) {
    tokens[count++] = token;
  }
  return count;
}

static int interactive(Client* client) {
  InputQueue queue;
  memset(&queue, 0, sizeof(queue));
  pthread_mutex_init(&queue.lock, NULL);
  pthread_t thread;
  if (pthread_create(&thread, NULL, input_thread, &queue) != 0) {
    log_error("cannot start the input thread");
    return 1;
  }
  pthread_detach(thread);

  printf("UDS tester on 0x%03lX -> 0x%03lX. Type 'help' for commands.\nuds> ",
         (unsigned long)client->link.config.tx_id, (unsigned long)client->link.config.rx_id);
  fflush(stdout);
  while (!g_stop) {
    if (!pump(client, 50)) {
      log_error("connection to the bus lost");
      return 1;
    }
    size_t unsolicited = 0;
    while (isotp_receive(&client->link, &unsolicited)) {
    }

    if (client->session != UDS_SESSION_DEFAULT &&
        pal_time_monotonic_ms() - client->last_request_ms >= TESTER_PRESENT_INTERVAL_MS) {
      const uint8_t keep_alive[] = {UDS_SID_TESTER_PRESENT, 0x00 | UDS_SUPPRESS_POSITIVE_RESPONSE};
      transact(client, keep_alive, sizeof(keep_alive), false);
    }

    char line[256];
    bool have_line = false;
    bool finished = false;
    pthread_mutex_lock(&queue.lock);
    if (queue.count > 0) {
      memcpy(line, queue.lines[queue.head], sizeof(line));
      queue.head = (queue.head + 1u) % INPUT_SLOTS;
      queue.count--;
      have_line = true;
    }
    finished = queue.end_of_input && queue.count == 0 && !have_line;
    pthread_mutex_unlock(&queue.lock);
    if (finished) {
      break;
    }
    if (!have_line) {
      continue;
    }
    char* tokens[MAX_TOKENS];
    const int count = tokenize(line, tokens);
    if (count > 0) {
      if (strcmp(tokens[0], "quit") == 0 || strcmp(tokens[0], "exit") == 0) {
        break;
      }
      run_command(client, count, tokens);
    }
    printf("uds> ");
    fflush(stdout);
  }
  printf("\n");
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

static void print_usage(const char* program) {
  printf("Usage: %s [options] [command [args] [+ command ...]]\n\n"
         "UDS tester (ISO 14229 over ISO-TP) for nodes on iotwin-bus. Without a command\n"
         "it reads commands from stdin and keeps a non-default session alive.\n\n"
         "Options:\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  --target ecu|gateway  light ECU (0x7E0/0x7E8, default) or NUCLEO gateway (0x7E1/0x7E9)\n"
         "  --tx ID --rx ID       other request / response ids (hex)\n"
         "  --timeout-ms N        response timeout (default %d)\n"
         "  --log-level LEVEL     debug shows every CAN frame\n"
         "  --help                show this help\n\n",
         program, DEFAULT_TIMEOUT_MS);
  print_command_help();
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  long port = 8080;
  long timeout_ms = DEFAULT_TIMEOUT_MS;
  uint32_t tx_id = 0x7E0;
  uint32_t rx_id = 0x7E8;
  int first_command = argc;

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : NULL;
    if (option[0] != '-') {
      first_command = i;
      break;
    }
    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(option, "--host") == 0 && value) {
      host = value;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value, 1, 65535, &port)) {
      i++;
    } else if (strcmp(option, "--timeout-ms") == 0 && cli_parse_long(value, 10, 60000, &timeout_ms)) {
      i++;
    } else if (strcmp(option, "--target") == 0 && value && strcmp(value, "ecu") == 0) {
      tx_id = 0x7E0;
      rx_id = 0x7E8;
      i++;
    } else if (strcmp(option, "--target") == 0 && value && strcmp(value, "gateway") == 0) {
      tx_id = 0x7E1;
      rx_id = 0x7E9;
      i++;
    } else if ((strcmp(option, "--tx") == 0 || strcmp(option, "--rx") == 0) && value &&
               strlen(value) <= 3 && strtoul(value, NULL, 16) <= CAN_SFF_MASK) {
      *(strcmp(option, "--tx") == 0 ? &tx_id : &rx_id) = (uint32_t)strtoul(value, NULL, 16);
      i++;
    } else if (strcmp(option, "--log-level") == 0 && value) {
      LogLevel level;
      if (!log_parse_level(value, &level)) {
        fprintf(stderr, "invalid log level '%s'\n", value);
        return 2;
      }
      log_set_level(level);
      i++;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    log_error("socket library initialisation failed: %s", net_last_error());
    return 1;
  }

  static Client client; /* large ISO-TP buffers: not on the stack */
  memset(&client, 0, sizeof(client));
  client.timeout_ms = (uint32_t)timeout_ms;
  client.session = UDS_SESSION_DEFAULT;
  client.socket = net_connect_tcp(host, (uint16_t)port);
  if (client.socket == NET_INVALID_SOCKET) {
    log_error("bus %s:%ld not reachable: %s", host, port, net_last_error());
    net_cleanup();
    return 1;
  }
  net_set_nonblocking(client.socket, true);
  line_reader_init(&client.reader);

  IsoTpConfig config;
  isotp_default_config(&config, tx_id, rx_id);
  config.send = link_send;
  config.context = &client;
  isotp_init(&client.link, &config, client.rx_buffer, sizeof(client.rx_buffer));

  int exit_code = 0;
  if (first_command >= argc) {
    exit_code = interactive(&client);
  } else {
    int start = first_command;
    while (start < argc && !g_stop && !client.connection_lost) {
      int end = start;
      while (end < argc && strcmp(argv[end], "+") != 0) {
        end++;
      }
      if (end > start) {
        const int result = run_command(&client, end - start, &argv[start]);
        if (result != 0) {
          exit_code = result;
          break;
        }
      }
      start = end + 1;
    }
  }

  net_close(client.socket);
  net_cleanup();
  return exit_code;
}
