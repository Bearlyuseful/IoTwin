#include "scenario.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scenario_init(Scenario* scenario) { memset(scenario, 0, sizeof(*scenario)); }

static bool fail(char* error, size_t size, int line, const char* format, ...) {
  if (error != NULL && size > 0) {
    char message[200];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    snprintf(error, size, "line %d: %s", line, message);
  }
  return false;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = (char)toupper((unsigned char)c);
  return (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
}

int scenario_parse_hex(const char* text, uint8_t* out, size_t capacity, bool* trailing_star) {
  size_t count = 0;
  int high = -1;
  if (trailing_star != NULL) {
    *trailing_star = false;
  }
  for (const char* p = text; *p != '\0'; p++) {
    if (*p == ' ' || *p == '.' || *p == '\t') {
      continue;
    }
    if (*p == '*' && p[1] == '\0' && trailing_star != NULL && high < 0) {
      *trailing_star = true;
      break;
    }
    const int digit = hex_digit(*p);
    if (digit < 0) {
      return -1;
    }
    if (high < 0) {
      high = digit;
    } else {
      if (count == capacity) {
        return -1;
      }
      out[count++] = (uint8_t)((high << 4) | digit);
      high = -1;
    }
  }
  return high < 0 ? (int)count : -1;
}

bool frame_pattern_parse(const char* text, FramePattern* pattern) {
  memset(pattern, 0, sizeof(*pattern));
  const char* hash = strchr(text, '#');
  if (hash == NULL) {
    return false;
  }
  const size_t id_length = (size_t)(hash - text);
  if (id_length != 3 && id_length != 8) {
    return false;
  }
  uint32_t id = 0;
  for (size_t i = 0; i < id_length; i++) {
    const int digit = hex_digit(text[i]);
    if (digit < 0) {
      return false;
    }
    id = (id << 4) | (uint32_t)digit;
  }
  pattern->extended = id_length == 8;
  if (id > (pattern->extended ? CAN_EFF_MASK : CAN_SFF_MASK)) {
    return false;
  }
  pattern->id = id;

  const char* payload = hash + 1;
  if (strcmp(payload, "*") == 0) {
    pattern->any_payload = true;
    return true;
  }
  if (payload[0] == 'R' || payload[0] == 'r') {
    pattern->rtr = true;
    return payload[1] == '\0';
  }
  size_t length = strlen(payload);
  if (length > 0 && payload[length - 1] == '*') {
    pattern->prefix = true;
    length--;
  }
  if (length % 2 != 0 || length / 2 > CAN_MAX_DLEN) {
    return false;
  }
  for (size_t i = 0; i < length; i += 2) {
    const size_t byte = i / 2;
    if ((payload[i] == 'X' || payload[i] == 'x') && (payload[i + 1] == 'X' || payload[i + 1] == 'x')) {
      pattern->wildcard |= (uint8_t)(1u << byte);
      continue;
    }
    const int high = hex_digit(payload[i]);
    const int low = hex_digit(payload[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    pattern->data[byte] = (uint8_t)((high << 4) | low);
  }
  pattern->length = (uint8_t)(length / 2);
  return true;
}

bool frame_pattern_matches(const FramePattern* pattern, const CanFrame* frame) {
  if (frame->id != pattern->id || frame->extended != pattern->extended || frame->rtr != pattern->rtr) {
    return false;
  }
  if (pattern->any_payload || pattern->rtr) {
    return true;
  }
  if (pattern->prefix ? frame->dlc < pattern->length : frame->dlc != pattern->length) {
    return false;
  }
  for (uint8_t i = 0; i < pattern->length; i++) {
    if (!(pattern->wildcard & (1u << i)) && frame->data[i] != pattern->data[i]) {
      return false;
    }
  }
  return true;
}

/* Splits off the next whitespace separated word; returns NULL at the end. */
static char* next_word(char** cursor) {
  char* p = *cursor;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p == '\0') {
    *cursor = p;
    return NULL;
  }
  char* start = p;
  while (*p != '\0' && *p != ' ' && *p != '\t') {
    p++;
  }
  if (*p != '\0') {
    *p++ = '\0';
  }
  *cursor = p;
  return start;
}

static char* rest_of_line(char** cursor) {
  char* p = *cursor;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  return p;
}

static bool parse_ms(const char* text, uint32_t* value) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  char* end = NULL;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (*end != '\0' || parsed > 3600000ul) {
    return false;
  }
  *value = (uint32_t)parsed;
  return true;
}

static bool parse_target(const char* text, uint32_t* tx, uint32_t* rx) {
  if (strcmp(text, "ecu") == 0) {
    *tx = 0x7E0;
    *rx = 0x7E8;
    return true;
  }
  if (strcmp(text, "gateway") == 0) {
    *tx = 0x7E1;
    *rx = 0x7E9;
    return true;
  }
  const char* colon = strchr(text, ':');
  if (colon == NULL) {
    return false;
  }
  char* end = NULL;
  *tx = (uint32_t)strtoul(text, &end, 16);
  if (end != colon) {
    return false;
  }
  *rx = (uint32_t)strtoul(colon + 1, &end, 16);
  return *end == '\0' && *tx <= CAN_SFF_MASK && *rx <= CAN_SFF_MASK;
}

bool scenario_parse_line(Scenario* scenario, const char* line, int line_number, char* error, size_t error_size) {
  char text[256];
  snprintf(text, sizeof(text), "%s", line);
  char* comment = strchr(text, '#');
  /* '#' inside a frame ("200#01") is not a comment: only at the start or after whitespace. */
  while (comment != NULL && comment != text && comment[-1] != ' ' && comment[-1] != '\t') {
    comment = strchr(comment + 1, '#');
  }
  if (comment != NULL) {
    *comment = '\0';
  }
  size_t length = strlen(text);
  while (length > 0 && isspace((unsigned char)text[length - 1])) {
    text[--length] = '\0';
  }

  char* cursor = text;
  char* keyword = next_word(&cursor);
  if (keyword == NULL) {
    return true; /* empty or comment */
  }
  if (strcmp(keyword, "name") == 0) {
    snprintf(scenario->name, sizeof(scenario->name), "%s", rest_of_line(&cursor));
    return true;
  }
  if (scenario->count == SCENARIO_MAX_STEPS) {
    return fail(error, error_size, line_number, "too many steps (max %d)", SCENARIO_MAX_STEPS);
  }
  ScenarioStep* step = &scenario->steps[scenario->count];
  memset(step, 0, sizeof(*step));
  step->line = line_number;

  if (strcmp(keyword, "send") == 0) {
    const char* frame = next_word(&cursor);
    if (frame == NULL || next_word(&cursor) != NULL || !can_frame_from_text(frame, strlen(frame), &step->frame)) {
      return fail(error, error_size, line_number, "usage: send <frame>, e.g. send 200#01");
    }
    step->type = STEP_SEND;
    snprintf(step->text, sizeof(step->text), "send %s", frame);
  } else if (strcmp(keyword, "expect") == 0 || strcmp(keyword, "expect-not") == 0) {
    const bool negative = strcmp(keyword, "expect-not") == 0;
    const char* pattern = next_word(&cursor);
    const char* qualifier = next_word(&cursor);
    const char* value = next_word(&cursor);
    if (pattern == NULL || !frame_pattern_parse(pattern, &step->pattern)) {
      return fail(error, error_size, line_number, "invalid frame pattern '%s'", pattern ? pattern : "");
    }
    step->ms = SCENARIO_DEFAULT_TIMEOUT_MS;
    if (negative && (qualifier == NULL || strcmp(qualifier, "for") != 0 || !parse_ms(value, &step->ms))) {
      return fail(error, error_size, line_number, "usage: expect-not <pattern> for <ms>");
    }
    if (!negative && qualifier != NULL && (strcmp(qualifier, "within") != 0 || !parse_ms(value, &step->ms))) {
      return fail(error, error_size, line_number, "usage: expect <pattern> [within <ms>]");
    }
    if (next_word(&cursor) != NULL) {
      return fail(error, error_size, line_number, "unexpected text after the step");
    }
    step->type = negative ? STEP_EXPECT_NOT : STEP_EXPECT;
    snprintf(step->text, sizeof(step->text), "%s %s %s %u ms", keyword, pattern, negative ? "for" : "within",
             (unsigned)step->ms);
  } else if (strcmp(keyword, "wait") == 0) {
    if (!parse_ms(next_word(&cursor), &step->ms) || next_word(&cursor) != NULL) {
      return fail(error, error_size, line_number, "usage: wait <ms>");
    }
    step->type = STEP_WAIT;
    snprintf(step->text, sizeof(step->text), "wait %u ms", (unsigned)step->ms);
  } else if (strcmp(keyword, "bench") == 0) {
    const char* target = next_word(&cursor);
    const char* command = rest_of_line(&cursor);
    if (target == NULL || *command == '\0') {
      return fail(error, error_size, line_number, "usage: bench <node> <command>, e.g. bench ecu fault short on");
    }
    step->type = STEP_BENCH;
    snprintf(step->text, sizeof(step->text), "@%s %s", target, command);
  } else if (strcmp(keyword, "bus") == 0) {
    const char* command = rest_of_line(&cursor);
    if (*command == '\0') {
      return fail(error, error_size, line_number, "usage: bus drop|delay|silence|seed|clear ...");
    }
    step->type = STEP_BUS;
    snprintf(step->text, sizeof(step->text), "fault %s", command);
  } else if (strcmp(keyword, "uds") == 0) {
    const char* target = next_word(&cursor);
    const char* request = next_word(&cursor);
    const char* expect = next_word(&cursor);
    const char* response = next_word(&cursor);
    if (target == NULL || !parse_target(target, &step->uds_tx, &step->uds_rx)) {
      return fail(error, error_size, line_number, "uds target must be ecu, gateway or TX:RX (hex)");
    }
    const int request_length =
        request ? scenario_parse_hex(request, step->uds_request, sizeof(step->uds_request), NULL) : -1;
    if (request_length <= 0) {
      return fail(error, error_size, line_number, "usage: uds <target> <request hex> [expect <response hex>[*]]");
    }
    step->uds_request_length = (size_t)request_length;
    if (expect != NULL) {
      const int expect_length = (strcmp(expect, "expect") == 0 && response != NULL)
                                    ? scenario_parse_hex(response, step->uds_expect, sizeof(step->uds_expect),
                                                         &step->uds_expect_prefix)
                                    : -1;
      if (expect_length < 0 || next_word(&cursor) != NULL) {
        return fail(error, error_size, line_number, "usage: uds <target> <request hex> [expect <response hex>[*]]");
      }
      step->uds_expect_given = true;
      step->uds_expect_length = (size_t)expect_length;
    }
    step->type = STEP_UDS;
    snprintf(step->text, sizeof(step->text), "uds %s %s%s%s", target, request, expect ? " expect " : "",
             expect ? response : "");
  } else if (strcmp(keyword, "log") == 0) {
    step->type = STEP_LOG;
    snprintf(step->text, sizeof(step->text), "%s", rest_of_line(&cursor));
  } else {
    return fail(error, error_size, line_number, "unknown step '%s'", keyword);
  }
  scenario->count++;
  return true;
}

bool scenario_load(Scenario* scenario, const char* path, char* error, size_t error_size) {
  scenario_init(scenario);
  FILE* file = fopen(path, "r");
  if (file == NULL) {
    snprintf(error, error_size, "cannot open %s", path);
    return false;
  }
  const char* base = strrchr(path, '/');
  const char* base_win = strrchr(path, '\\');
  if (base_win != NULL && (base == NULL || base_win > base)) {
    base = base_win;
  }
  snprintf(scenario->name, sizeof(scenario->name), "%s", base != NULL ? base + 1 : path);

  char line[256];
  int line_number = 0;
  bool ok = true;
  while (ok && fgets(line, sizeof(line), file) != NULL) {
    line_number++;
    line[strcspn(line, "\r\n")] = '\0';
    ok = scenario_parse_line(scenario, line, line_number, error, error_size);
  }
  fclose(file);
  return ok;
}
