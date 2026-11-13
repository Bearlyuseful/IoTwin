#include "uds.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* DTC helpers                                                                */
/* ------------------------------------------------------------------------- */

void uds_dtc_init(UdsDtc* dtc, uint32_t code) {
  dtc->code = code & 0xFFFFFFu;
  dtc->status = UDS_DTC_NOT_COMPLETED_SINCE_CLEAR | UDS_DTC_NOT_COMPLETED_THIS_CYCLE;
}

void uds_dtc_report(UdsDtc* dtc, bool failed) {
  uint8_t status = dtc->status;
  status &= (uint8_t)~(UDS_DTC_NOT_COMPLETED_SINCE_CLEAR | UDS_DTC_NOT_COMPLETED_THIS_CYCLE);
  if (failed) {
    status |= UDS_DTC_TEST_FAILED | UDS_DTC_TEST_FAILED_THIS_CYCLE | UDS_DTC_PENDING |
              UDS_DTC_CONFIRMED | UDS_DTC_FAILED_SINCE_CLEAR;
  } else {
    status &= (uint8_t)~UDS_DTC_TEST_FAILED;
  }
  dtc->status = status;
}

void uds_dtc_start_operation_cycle(UdsDtc* dtc) {
  dtc->status = (uint8_t)((dtc->status & ~(UDS_DTC_TEST_FAILED_THIS_CYCLE | UDS_DTC_TEST_FAILED)) |
                          UDS_DTC_NOT_COMPLETED_THIS_CYCLE);
}

UdsDtc* uds_server_find_dtc(UdsServer* server, uint32_t code) {
  for (size_t i = 0; i < server->config.dtc_count; i++) {
    if (server->config.dtcs[i].code == (code & 0xFFFFFFu)) {
      return &server->config.dtcs[i];
    }
  }
  return NULL;
}

/* ------------------------------------------------------------------------- */
/* Server                                                                     */
/* ------------------------------------------------------------------------- */

void uds_server_default_config(UdsServerConfig* config, uint32_t request_id, uint32_t response_id) {
  memset(config, 0, sizeof(*config));
  config->request_id = request_id;
  config->response_id = response_id;
  config->functional_id = UDS_FUNCTIONAL_ID;
  config->p2_ms = UDS_DEFAULT_P2_MS;
  config->p2_star_ms = UDS_DEFAULT_P2_STAR_MS;
  config->s3_ms = UDS_DEFAULT_S3_MS;
}

void uds_server_init(UdsServer* server, const UdsServerConfig* config) {
  memset(server, 0, sizeof(*server));
  server->config = *config;
  server->session = UDS_SESSION_DEFAULT;

  IsoTpConfig link_config;
  isotp_default_config(&link_config, config->response_id, config->request_id);
  link_config.extended_id = config->extended_id;
  link_config.send = config->send;
  link_config.context = config->send_context;
  isotp_init(&server->link, &link_config, server->request, sizeof(server->request));
}

UdsSession uds_server_session(const UdsServer* server) {
  return server->session;
}

static void set_session(UdsServer* server, UdsSession session) {
  if (server->session == session) {
    return;
  }
  server->session = session;
  if (server->config.handlers.session_changed != NULL) {
    server->config.handlers.session_changed(server->config.handlers.context, session);
  }
}

static void put_u16(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
}

static void put_u24(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value >> 16);
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)value;
}

/* Each service handler returns the response length and sets *nrc on failure. */

static size_t session_control(UdsServer* server, const uint8_t* request, size_t length,
                              uint8_t* response, uint8_t* nrc) {
  if (length != 2) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  const uint8_t session = request[1] & 0x7Fu;
  if (session != UDS_SESSION_DEFAULT && session != UDS_SESSION_EXTENDED) {
    *nrc = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    return 0;
  }
  set_session(server, (UdsSession)session);
  response[1] = session;
  put_u16(&response[2], server->config.p2_ms);
  put_u16(&response[4], server->config.p2_star_ms / 10u);
  return 6;
}

static size_t ecu_reset(UdsServer* server, const uint8_t* request, size_t length, uint8_t* response,
                        uint8_t* nrc) {
  if (length != 2) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  const uint8_t type = request[1] & 0x7Fu;
  if ((type != UDS_RESET_HARD && type != UDS_RESET_SOFT) || server->config.handlers.reset == NULL) {
    *nrc = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    return 0;
  }
  server->reset_pending = true;
  server->reset_type = type;
  response[1] = type;
  return 2;
}

static size_t tester_present(const uint8_t* request, size_t length, uint8_t* response, uint8_t* nrc) {
  if (length != 2) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  if ((request[1] & 0x7Fu) != 0x00u) {
    *nrc = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    return 0;
  }
  response[1] = 0x00;
  return 2;
}

static size_t read_data(UdsServer* server, const uint8_t* request, size_t length, uint8_t* response,
                        size_t capacity, uint8_t* nrc) {
  if (length < 3 || (length - 1u) % 2u != 0 || (length - 1u) / 2u > UDS_MAX_DIDS_PER_REQUEST) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  size_t position = 1;
  size_t supported = 0;
  for (size_t i = 1; i + 1 < length; i += 2) {
    const uint16_t did = (uint16_t)((request[i] << 8) | request[i + 1]);
    if (capacity < position + 3u) {
      *nrc = UDS_NRC_RESPONSE_TOO_LONG;
      return 0;
    }
    uint8_t* data = &response[position + 2];
    const size_t room = capacity - position - 2u;
    int written;
    if (did == UDS_DID_ACTIVE_SESSION) {
      data[0] = (uint8_t)server->session;
      written = 1;
    } else if (server->config.handlers.read_did != NULL) {
      written = server->config.handlers.read_did(server->config.handlers.context, did, data, room);
    } else {
      written = -(int)UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    if (written == -(int)UDS_NRC_REQUEST_OUT_OF_RANGE) {
      continue; /* unsupported identifiers are skipped */
    }
    if (written < 0) {
      *nrc = (uint8_t)(-written);
      return 0;
    }
    if ((size_t)written > room) {
      *nrc = UDS_NRC_RESPONSE_TOO_LONG;
      return 0;
    }
    put_u16(&response[position], did);
    position += 2u + (size_t)written;
    supported++;
  }
  if (supported == 0) {
    *nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    return 0;
  }
  return position;
}

static size_t write_data(UdsServer* server, const uint8_t* request, size_t length, uint8_t* response,
                         uint8_t* nrc) {
  if (server->config.handlers.write_did == NULL) {
    *nrc = UDS_NRC_SERVICE_NOT_SUPPORTED;
    return 0;
  }
  if (length < 4) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  if (server->session != UDS_SESSION_EXTENDED) {
    *nrc = UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION;
    return 0;
  }
  const uint16_t did = (uint16_t)((request[1] << 8) | request[2]);
  const uint8_t result = server->config.handlers.write_did(server->config.handlers.context, did,
                                                           &request[3], length - 3u);
  if (result != 0) {
    *nrc = result;
    return 0;
  }
  put_u16(&response[1], did);
  return 3;
}

static size_t clear_dtcs(UdsServer* server, const uint8_t* request, size_t length, uint8_t* nrc) {
  if (length != 4) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  const uint32_t group = ((uint32_t)request[1] << 16) | ((uint32_t)request[2] << 8) | request[3];
  if (group == 0xFFFFFFu) {
    for (size_t i = 0; i < server->config.dtc_count; i++) {
      uds_dtc_init(&server->config.dtcs[i], server->config.dtcs[i].code);
    }
    return 1;
  }
  UdsDtc* dtc = uds_server_find_dtc(server, group);
  if (dtc == NULL) {
    *nrc = UDS_NRC_REQUEST_OUT_OF_RANGE;
    return 0;
  }
  uds_dtc_init(dtc, dtc->code);
  return 1;
}

static size_t read_dtcs(UdsServer* server, const uint8_t* request, size_t length, uint8_t* response,
                        size_t capacity, uint8_t* nrc) {
  if (length < 2) {
    *nrc = UDS_NRC_INCORRECT_LENGTH;
    return 0;
  }
  const uint8_t report = request[1] & 0x7Fu;
  uint8_t mask = UDS_DTC_STATUS_AVAILABILITY;
  if (report == 0x01u || report == 0x02u) {
    if (length != 3) {
      *nrc = UDS_NRC_INCORRECT_LENGTH;
      return 0;
    }
    mask = request[2];
  } else if (report == 0x0Au) {
    if (length != 2) {
      *nrc = UDS_NRC_INCORRECT_LENGTH;
      return 0;
    }
  } else {
    *nrc = UDS_NRC_SUBFUNCTION_NOT_SUPPORTED;
    return 0;
  }

  response[1] = report;
  response[2] = UDS_DTC_STATUS_AVAILABILITY;
  if (report == 0x01u) {
    uint32_t count = 0;
    for (size_t i = 0; i < server->config.dtc_count; i++) {
      if ((server->config.dtcs[i].status & mask & UDS_DTC_STATUS_AVAILABILITY) != 0) {
        count++;
      }
    }
    response[3] = UDS_DTC_FORMAT_ISO14229;
    put_u16(&response[4], count);
    return 6;
  }

  size_t position = 3;
  for (size_t i = 0; i < server->config.dtc_count; i++) {
    const UdsDtc* dtc = &server->config.dtcs[i];
    if (report == 0x02u && (dtc->status & mask & UDS_DTC_STATUS_AVAILABILITY) == 0) {
      continue;
    }
    if (position + 4u > capacity) {
      *nrc = UDS_NRC_RESPONSE_TOO_LONG;
      return 0;
    }
    put_u24(&response[position], dtc->code);
    response[position + 3] = dtc->status & UDS_DTC_STATUS_AVAILABILITY;
    position += 4;
  }
  return position;
}

static bool suppressible(uint8_t sid) {
  return sid == UDS_SID_DIAGNOSTIC_SESSION_CONTROL || sid == UDS_SID_ECU_RESET ||
         sid == UDS_SID_TESTER_PRESENT;
}

size_t uds_server_process(UdsServer* server, const uint8_t* request, size_t length, bool functional,
                          uint8_t* response, size_t capacity, uint64_t now_us) {
  if (length == 0 || capacity < 8) {
    return 0;
  }
  server->requests++;
  server->last_request_us = now_us;

  const uint8_t sid = request[0];
  uint8_t nrc = 0;
  size_t written = 0;
  response[0] = (uint8_t)(sid + UDS_POSITIVE_RESPONSE_OFFSET);
  switch (sid) {
    case UDS_SID_DIAGNOSTIC_SESSION_CONTROL:
      written = session_control(server, request, length, response, &nrc);
      break;
    case UDS_SID_ECU_RESET:
      written = ecu_reset(server, request, length, response, &nrc);
      break;
    case UDS_SID_TESTER_PRESENT:
      written = tester_present(request, length, response, &nrc);
      break;
    case UDS_SID_READ_DATA_BY_IDENTIFIER:
      written = read_data(server, request, length, response, capacity, &nrc);
      break;
    case UDS_SID_WRITE_DATA_BY_IDENTIFIER:
      written = write_data(server, request, length, response, &nrc);
      break;
    case UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION:
      written = clear_dtcs(server, request, length, &nrc);
      break;
    case UDS_SID_READ_DTC_INFORMATION:
      written = read_dtcs(server, request, length, response, capacity, &nrc);
      break;
    default:
      nrc = UDS_NRC_SERVICE_NOT_SUPPORTED;
      break;
  }

  if (nrc != 0) {
    if (functional && (nrc == UDS_NRC_SERVICE_NOT_SUPPORTED || nrc == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED ||
                       nrc == UDS_NRC_REQUEST_OUT_OF_RANGE ||
                       nrc == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION ||
                       nrc == UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION)) {
      return 0;
    }
    server->negative_responses++;
    response[0] = UDS_SID_NEGATIVE_RESPONSE;
    response[1] = sid;
    response[2] = nrc;
    return 3;
  }
  if (suppressible(sid) && length >= 2 && (request[1] & UDS_SUPPRESS_POSITIVE_RESPONSE) != 0) {
    return 0;
  }
  return written;
}

static void respond(UdsServer* server, const uint8_t* request, size_t length, bool functional,
                    uint64_t now_us) {
  const size_t response_length = uds_server_process(server, request, length, functional, server->response,
                                                    sizeof(server->response), now_us);
  if (response_length > 0) {
    /* A busy link (previous response still in progress) drops the response, like a busy ECU. */
    (void)isotp_send(&server->link, server->response, response_length, now_us);
  }
}

bool uds_server_on_frame(UdsServer* server, const CanFrame* frame, uint64_t now_us) {
  if (frame->rtr || frame->extended != server->config.extended_id) {
    return false;
  }
  if (server->config.functional_id != 0 && frame->id == server->config.functional_id) {
    /* Functional requests: single frames only. */
    const uint8_t pci = frame->dlc > 0 ? frame->data[0] : 0xFFu;
    const size_t length = pci & 0x0Fu;
    if ((pci & 0xF0u) == 0x00u && length >= 1 && length + 1u <= frame->dlc) {
      uint8_t request[7];
      memcpy(request, &frame->data[1], length);
      respond(server, request, length, true, now_us);
    }
    return true;
  }
  if (frame->id != server->config.request_id) {
    return false;
  }
  isotp_on_frame(&server->link, frame, now_us);
  size_t length = 0;
  if (isotp_receive(&server->link, &length)) {
    /* Processed right away, before the next frame can overwrite the receive buffer. */
    respond(server, server->request, length, false, now_us);
  }
  return true;
}

void uds_server_poll(UdsServer* server, uint64_t now_us) {
  isotp_poll(&server->link, now_us);

  if (server->reset_pending && !isotp_tx_busy(&server->link)) {
    server->reset_pending = false;
    set_session(server, UDS_SESSION_DEFAULT);
    server->config.handlers.reset(server->config.handlers.context, server->reset_type);
  }

  if (server->session != UDS_SESSION_DEFAULT &&
      now_us - server->last_request_us > (uint64_t)server->config.s3_ms * 1000u) {
    set_session(server, UDS_SESSION_DEFAULT);
  }
}

/* ------------------------------------------------------------------------- */
/* Text helpers                                                               */
/* ------------------------------------------------------------------------- */

static const char* request_name(uint8_t sid) {
  switch (sid) {
    case UDS_SID_DIAGNOSTIC_SESSION_CONTROL: return "DiagnosticSessionControl";
    case UDS_SID_ECU_RESET: return "ECUReset";
    case UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION: return "ClearDiagnosticInformation";
    case UDS_SID_READ_DTC_INFORMATION: return "ReadDTCInformation";
    case UDS_SID_READ_DATA_BY_IDENTIFIER: return "ReadDataByIdentifier";
    case 0x23: return "ReadMemoryByAddress";
    case 0x27: return "SecurityAccess";
    case 0x28: return "CommunicationControl";
    case UDS_SID_WRITE_DATA_BY_IDENTIFIER: return "WriteDataByIdentifier";
    case 0x2F: return "InputOutputControlByIdentifier";
    case 0x31: return "RoutineControl";
    case 0x34: return "RequestDownload";
    case 0x36: return "TransferData";
    case 0x37: return "RequestTransferExit";
    case UDS_SID_TESTER_PRESENT: return "TesterPresent";
    case 0x85: return "ControlDTCSetting";
    case UDS_SID_NEGATIVE_RESPONSE: return "NegativeResponse";
    default: return NULL;
  }
}

const char* uds_service_name(uint8_t sid) {
  const char* name = request_name(sid);
  if (name == NULL && (sid & UDS_POSITIVE_RESPONSE_OFFSET) != 0) {
    name = request_name((uint8_t)(sid - UDS_POSITIVE_RESPONSE_OFFSET)); /* positive response */
  }
  return name != NULL ? name : "unknown service";
}

const char* uds_nrc_name(uint8_t nrc) {
  switch (nrc) {
    case UDS_NRC_GENERAL_REJECT: return "generalReject";
    case UDS_NRC_SERVICE_NOT_SUPPORTED: return "serviceNotSupported";
    case UDS_NRC_SUBFUNCTION_NOT_SUPPORTED: return "subFunctionNotSupported";
    case UDS_NRC_INCORRECT_LENGTH: return "incorrectMessageLengthOrInvalidFormat";
    case UDS_NRC_RESPONSE_TOO_LONG: return "responseTooLong";
    case UDS_NRC_BUSY_REPEAT_REQUEST: return "busyRepeatRequest";
    case UDS_NRC_CONDITIONS_NOT_CORRECT: return "conditionsNotCorrect";
    case UDS_NRC_REQUEST_OUT_OF_RANGE: return "requestOutOfRange";
    case UDS_NRC_SECURITY_ACCESS_DENIED: return "securityAccessDenied";
    case UDS_NRC_RESPONSE_PENDING: return "requestCorrectlyReceived-ResponsePending";
    case UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION: return "subFunctionNotSupportedInActiveSession";
    case UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION: return "serviceNotSupportedInActiveSession";
    default: return "unknown NRC";
  }
}

const char* uds_session_name(uint8_t session) {
  switch (session) {
    case UDS_SESSION_DEFAULT: return "default";
    case UDS_SESSION_PROGRAMMING: return "programming";
    case UDS_SESSION_EXTENDED: return "extended";
    default: return "unknown";
  }
}

static const char kHex[] = "0123456789ABCDEF";

void uds_format_dtc(uint32_t code, char* out, size_t capacity) {
  if (capacity < 9) {
    if (capacity > 0) {
      out[0] = '\0';
    }
    return;
  }
  static const char kSystem[] = "PCBU";
  out[0] = kSystem[(code >> 22) & 0x3u];
  out[1] = (char)('0' + ((code >> 20) & 0x3u));
  out[2] = kHex[(code >> 16) & 0xFu];
  out[3] = kHex[(code >> 12) & 0xFu];
  out[4] = kHex[(code >> 8) & 0xFu];
  out[5] = '-';
  out[6] = kHex[(code >> 4) & 0xFu];
  out[7] = kHex[code & 0xFu];
  out[8] = '\0';
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static bool parse_hex(const char* text, size_t digits, uint32_t* value) {
  uint32_t result = 0;
  for (size_t i = 0; i < digits; i++) {
    const int nibble = hex_value(text[i]);
    if (nibble < 0) {
      return false;
    }
    result = (result << 4) | (uint32_t)nibble;
  }
  *value = result;
  return true;
}

bool uds_parse_dtc(const char* text, uint32_t* code) {
  const size_t length = strlen(text);
  uint32_t value = 0;
  if (length == 6 && parse_hex(text, 6, &value)) {
    *code = value;
    return true;
  }
  if (length != 5 && !(length == 8 && text[5] == '-')) {
    return false;
  }
  static const char kSystem[] = "PCBU";
  const char letter = (char)(text[0] >= 'a' ? text[0] - ('a' - 'A') : text[0]);
  const char* system = memchr(kSystem, letter, 4);
  if (system == NULL || text[1] < '0' || text[1] > '3') {
    return false;
  }
  uint32_t digits = 0;
  uint32_t failure_type = 0;
  if (!parse_hex(&text[2], 3, &digits) || (length == 8 && !parse_hex(&text[6], 2, &failure_type))) {
    return false;
  }
  *code = ((uint32_t)(system - kSystem) << 22) | ((uint32_t)(text[1] - '0') << 20) | (digits << 8) |
          failure_type;
  return true;
}
