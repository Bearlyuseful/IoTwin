#include "dbc.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LENGTH 8192
#define EXTENDED_FLAG 0x80000000u

static void set_error(char* error, size_t size, unsigned line, const char* format, ...) {
  if (error == NULL || size == 0) {
    return;
  }
  int n = snprintf(error, size, "line %u: ", line);
  if (n < 0 || (size_t)n >= size) {
    return;
  }
  va_list args;
  va_start(args, format);
  vsnprintf(error + n, size - (size_t)n, format, args);
  va_end(args);
}

static const char* skip_spaces(const char* p) {
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  return p;
}

/* Reads a C identifier; returns the number of characters consumed (0 if none). */
static size_t read_identifier(const char* p, char* out, size_t out_size) {
  size_t n = 0;
  while (isalnum((unsigned char)p[n]) || p[n] == '_') {
    if (n + 1 < out_size) {
      out[n] = p[n];
    }
    n++;
  }
  out[n < out_size ? n : out_size - 1] = '\0';
  return n;
}

static bool starts_with_keyword(const char* p, const char* keyword) {
  size_t length = strlen(keyword);
  return strncmp(p, keyword, length) == 0 && (p[length] == ' ' || p[length] == '\t' || p[length] == ':');
}

static bool parse_message(const char* p, DbcDatabase* db, unsigned line, char* error, size_t error_size) {
  char* end = NULL;
  p = skip_spaces(p);
  unsigned long long raw_id = strtoull(p, &end, 10);
  if (end == p) {
    set_error(error, error_size, line, "BO_ without message id");
    return false;
  }
  DbcMessage message;
  memset(&message, 0, sizeof(message));
  message.extended = (raw_id & EXTENDED_FLAG) != 0;
  message.id = (uint32_t)(raw_id & CAN_EFF_MASK);
  if (!message.extended && message.id > CAN_SFF_MASK) {
    set_error(error, error_size, line, "standard id %llu out of range", raw_id);
    return false;
  }

  p = skip_spaces(end);
  size_t n = read_identifier(p, message.name, sizeof(message.name));
  if (n == 0) {
    set_error(error, error_size, line, "BO_ without message name");
    return false;
  }
  p = skip_spaces(p + n);
  if (*p != ':') {
    set_error(error, error_size, line, "expected ':' after message name");
    return false;
  }
  p = skip_spaces(p + 1);
  unsigned long dlc = strtoul(p, &end, 10);
  if (end == p || dlc > 64) {
    set_error(error, error_size, line, "invalid DLC");
    return false;
  }
  message.dlc = (uint8_t)(dlc > CAN_MAX_DLEN ? CAN_MAX_DLEN : dlc);
  p = skip_spaces(end);
  read_identifier(p, message.transmitter, sizeof(message.transmitter));

  DbcMessage* grown = realloc(db->messages, (db->message_count + 1) * sizeof(DbcMessage));
  if (grown == NULL) {
    set_error(error, error_size, line, "out of memory");
    return false;
  }
  db->messages = grown;
  db->messages[db->message_count++] = message;
  return true;
}

static bool parse_signal(const char* p, DbcMessage* message, unsigned line, char* error, size_t error_size) {
  DbcSignal signal;
  memset(&signal, 0, sizeof(signal));

  p = skip_spaces(p);
  size_t n = read_identifier(p, signal.name, sizeof(signal.name));
  if (n == 0) {
    set_error(error, error_size, line, "SG_ without signal name");
    return false;
  }
  p = skip_spaces(p + n);

  if (*p != ':') {
    /* multiplexing indicator: "M" or "m<value>" (or "m<value>M") */
    if (*p == 'M') {
      signal.is_multiplexer = true;
      p++;
    } else if (*p == 'm') {
      char* end = NULL;
      unsigned long value = strtoul(p + 1, &end, 10);
      if (end == p + 1) {
        set_error(error, error_size, line, "invalid multiplexer indicator");
        return false;
      }
      signal.is_multiplexed = true;
      signal.mux_value = (uint32_t)value;
      p = end;
      if (*p == 'M') {
        signal.is_multiplexer = true;
        p++;
      }
    } else {
      set_error(error, error_size, line, "expected ':' after signal name");
      return false;
    }
    p = skip_spaces(p);
    if (*p != ':') {
      set_error(error, error_size, line, "expected ':' after multiplexer indicator");
      return false;
    }
  }
  p++;

  unsigned start = 0;
  unsigned length = 0;
  char order = 0;
  char sign = 0;
  int consumed = 0;
  if (sscanf(p, " %u|%u@%c%c (%lf,%lf) [%lf|%lf]%n", &start, &length, &order, &sign, &signal.signal.factor,
             &signal.signal.offset, &signal.minimum, &signal.maximum, &consumed) != 8 ||
      consumed == 0) {
    set_error(error, error_size, line, "malformed signal definition of %s", signal.name);
    return false;
  }
  if ((order != '0' && order != '1') || (sign != '+' && sign != '-') || length == 0 || length > 64 ||
      start > 63) {
    set_error(error, error_size, line, "invalid layout of signal %s", signal.name);
    return false;
  }
  signal.signal.start_bit = (uint16_t)start;
  signal.signal.length = (uint8_t)length;
  signal.signal.byte_order = order == '1' ? CAN_BYTE_ORDER_INTEL : CAN_BYTE_ORDER_MOTOROLA;
  signal.signal.is_signed = sign == '-';
  if (!can_signal_layout_valid(&signal.signal)) {
    set_error(error, error_size, line, "signal %s does not fit into 8 bytes", signal.name);
    return false;
  }

  p = skip_spaces(p + consumed);
  if (*p == '"') {
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
      if (i + 1 < sizeof(signal.unit)) {
        signal.unit[i++] = *p;
      }
      p++;
    }
    signal.unit[i] = '\0';
  }

  DbcSignal* grown = realloc(message->signals, (message->signal_count + 1) * sizeof(DbcSignal));
  if (grown == NULL) {
    set_error(error, error_size, line, "out of memory");
    return false;
  }
  message->signals = grown;
  message->signals[message->signal_count++] = signal;
  return true;
}

static bool parse_values(const char* p, DbcDatabase* db, unsigned line, char* error, size_t error_size) {
  char* end = NULL;
  p = skip_spaces(p);
  unsigned long long raw_id = strtoull(p, &end, 10);
  if (end == p) {
    return true; /* value table definition (VAL_TABLE_ style) - not needed */
  }
  char name[DBC_NAME_MAX];
  p = skip_spaces(end);
  size_t n = read_identifier(p, name, sizeof(name));
  if (n == 0) {
    set_error(error, error_size, line, "VAL_ without signal name");
    return false;
  }
  p += n;

  const DbcMessage* message =
      dbc_find_message(db, (uint32_t)(raw_id & CAN_EFF_MASK), (raw_id & EXTENDED_FLAG) != 0);
  DbcSignal* signal = message ? (DbcSignal*)dbc_find_signal(message, name) : NULL;

  for (;;) {
    p = skip_spaces(p);
    if (*p == ';' || *p == '\0') {
      break;
    }
    long long value = strtoll(p, &end, 10);
    if (end == p) {
      set_error(error, error_size, line, "malformed value description");
      return false;
    }
    p = skip_spaces(end);
    if (*p != '"') {
      set_error(error, error_size, line, "expected quoted value description");
      return false;
    }
    p++;
    DbcValueName entry;
    entry.value = value;
    size_t i = 0;
    while (*p && *p != '"') {
      if (i + 1 < sizeof(entry.text)) {
        entry.text[i++] = *p;
      }
      p++;
    }
    entry.text[i] = '\0';
    if (*p == '"') {
      p++;
    }
    if (signal != NULL) {
      DbcValueName* grown = realloc(signal->values, (signal->value_count + 1) * sizeof(DbcValueName));
      if (grown == NULL) {
        set_error(error, error_size, line, "out of memory");
        return false;
      }
      signal->values = grown;
      signal->values[signal->value_count++] = entry;
    }
  }
  return true;
}

bool dbc_parse(const char* text, DbcDatabase* database, char* error, size_t error_size) {
  memset(database, 0, sizeof(*database));
  if (error && error_size) {
    error[0] = '\0';
  }

  char* line = malloc(LINE_MAX_LENGTH);
  if (line == NULL) {
    return false;
  }
  bool ok = true;
  bool in_string = false; /* inside a multi-line quoted string of a skipped statement */
  unsigned line_number = 0;
  const char* p = text;

  while (ok && *p) {
    line_number++;
    size_t length = strcspn(p, "\r\n");
    if (length >= LINE_MAX_LENGTH) {
      set_error(error, error_size, line_number, "line too long");
      ok = false;
      break;
    }
    memcpy(line, p, length);
    line[length] = '\0';
    p += length;
    if (*p == '\r') {
      p++;
    }
    if (*p == '\n') {
      p++;
    }

    const char* l = skip_spaces(line);
    if (in_string) {
      for (const char* c = l; *c; c++) {
        if (*c == '"') {
          in_string = !in_string;
        }
      }
      continue;
    }

    if (starts_with_keyword(l, "BO_")) {
      ok = parse_message(l + 3, database, line_number, error, error_size);
    } else if (starts_with_keyword(l, "SG_")) {
      if (database->message_count == 0) {
        set_error(error, error_size, line_number, "SG_ outside of a message");
        ok = false;
      } else {
        ok = parse_signal(l + 3, &database->messages[database->message_count - 1], line_number, error,
                          error_size);
      }
    } else if (starts_with_keyword(l, "VAL_")) {
      ok = parse_values(l + 4, database, line_number, error, error_size);
    } else {
      /* skipped statement: track quotes so multi-line comments are skipped completely */
      for (const char* c = l; *c; c++) {
        if (*c == '"') {
          in_string = !in_string;
        }
      }
    }
  }

  free(line);
  if (!ok) {
    dbc_free(database);
  }
  return ok;
}

bool dbc_load_file(const char* path, DbcDatabase* database, char* error, size_t error_size) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    if (error && error_size) {
      snprintf(error, error_size, "cannot open %s", path);
    }
    return false;
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size < 0 || size > 64L * 1024 * 1024) {
    fclose(file);
    if (error && error_size) {
      snprintf(error, error_size, "cannot read %s", path);
    }
    return false;
  }
  char* text = malloc((size_t)size + 1);
  if (text == NULL) {
    fclose(file);
    return false;
  }
  size_t read = fread(text, 1, (size_t)size, file);
  fclose(file);
  text[read] = '\0';
  bool ok = dbc_parse(text, database, error, error_size);
  free(text);
  return ok;
}

void dbc_free(DbcDatabase* database) {
  if (database == NULL) {
    return;
  }
  for (size_t m = 0; m < database->message_count; m++) {
    DbcMessage* message = &database->messages[m];
    for (size_t s = 0; s < message->signal_count; s++) {
      free(message->signals[s].values);
    }
    free(message->signals);
  }
  free(database->messages);
  database->messages = NULL;
  database->message_count = 0;
}

const DbcMessage* dbc_find_message(const DbcDatabase* database, uint32_t id, bool extended) {
  for (size_t i = 0; i < database->message_count; i++) {
    if (database->messages[i].id == id && database->messages[i].extended == extended) {
      return &database->messages[i];
    }
  }
  return NULL;
}

const DbcMessage* dbc_find_message_by_name(const DbcDatabase* database, const char* name) {
  for (size_t i = 0; i < database->message_count; i++) {
    if (strcmp(database->messages[i].name, name) == 0) {
      return &database->messages[i];
    }
  }
  return NULL;
}

const DbcSignal* dbc_find_signal(const DbcMessage* message, const char* name) {
  for (size_t i = 0; i < message->signal_count; i++) {
    if (strcmp(message->signals[i].name, name) == 0) {
      return &message->signals[i];
    }
  }
  return NULL;
}

const char* dbc_value_name(const DbcSignal* signal, int64_t raw) {
  for (size_t i = 0; i < signal->value_count; i++) {
    if (signal->values[i].value == raw) {
      return signal->values[i].text;
    }
  }
  return NULL;
}

bool dbc_signal_present(const DbcMessage* message, const DbcSignal* signal, const uint8_t data[8]) {
  if (!signal->is_multiplexed) {
    return true;
  }
  for (size_t i = 0; i < message->signal_count; i++) {
    const DbcSignal* multiplexer = &message->signals[i];
    if (multiplexer->is_multiplexer && multiplexer != signal) {
      return can_signal_extract_raw(&multiplexer->signal, data) == signal->mux_value;
    }
  }
  return true;
}

int dbc_describe_frame(const DbcDatabase* database, const CanFrame* frame, char* out, size_t out_size) {
  const DbcMessage* message = dbc_find_message(database, frame->id, frame->extended);
  if (message == NULL || out == NULL || out_size == 0) {
    return -1;
  }
  size_t used = 0;
  int n = snprintf(out, out_size, "%s", message->name);
  if (n < 0) {
    return -1;
  }
  used = (size_t)n < out_size ? (size_t)n : out_size - 1;
  if (frame->rtr) {
    n = snprintf(out + used, out_size - used, " (remote request)");
    used += (n > 0 && (size_t)n < out_size - used) ? (size_t)n : 0;
    return (int)used;
  }

  for (size_t i = 0; i < message->signal_count && used + 1 < out_size; i++) {
    const DbcSignal* signal = &message->signals[i];
    if (!dbc_signal_present(message, signal, frame->data)) {
      continue;
    }
    const uint64_t raw = can_signal_extract_raw(&signal->signal, frame->data);
    const double value = can_signal_decode(&signal->signal, frame->data);
    const char* name = dbc_value_name(signal, can_signal_raw_to_int(&signal->signal, raw));
    n = snprintf(out + used, out_size - used, " %s=%g%s%s%s%s%s", signal->name, value,
                 signal->unit[0] ? " " : "", signal->unit, name ? " (" : "", name ? name : "",
                 name ? ")" : "");
    if (n < 0) {
      break;
    }
    used += (size_t)n < out_size - used ? (size_t)n : out_size - used - 1;
  }
  return (int)used;
}

bool dbc_set_signal(const DbcMessage* message, const char* signal_name, double physical, CanFrame* frame) {
  const DbcSignal* signal = dbc_find_signal(message, signal_name);
  if (signal == NULL) {
    return false;
  }
  frame->id = message->id;
  frame->extended = message->extended;
  frame->rtr = false;
  if (frame->dlc < message->dlc) {
    frame->dlc = message->dlc;
  }
  return can_signal_encode(&signal->signal, frame->data, physical);
}
