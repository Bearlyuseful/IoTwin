#include "candump_log.h"

#include <stdio.h>
#include <string.h>

int candump_log_format(const CanFrame* frame, uint64_t time_us, const char* interface, char* out, size_t size) {
  char text[CAN_FRAME_TEXT_MAX];
  if (can_frame_to_text(frame, text, sizeof(text)) < 0) {
    return -1;
  }
  const int length = snprintf(out, size, "(%llu.%06llu) %s %s", (unsigned long long)(time_us / 1000000u),
                              (unsigned long long)(time_us % 1000000u), interface ? interface : "iotwin", text);
  return (length < 0 || (size_t)length >= size) ? -1 : length;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

CandumpLineType candump_log_parse(const char* line, size_t length, CanFrame* frame, uint64_t* time_us,
                                  bool* has_time) {
  size_t pos = 0;
  while (pos < length && is_space(line[pos])) {
    pos++;
  }
  while (length > pos && is_space(line[length - 1])) {
    length--;
  }
  *time_us = 0;
  if (has_time != NULL) {
    *has_time = false;
  }
  if (pos == length || line[pos] == '#' || line[pos] == ';') {
    return CANDUMP_LINE_SKIP;
  }

  if (line[pos] == '(') {
    /* (seconds.fraction) - up to 6 fraction digits are used */
    pos++;
    uint64_t seconds = 0;
    uint64_t micros = 0;
    size_t digits = 0;
    while (pos < length && line[pos] >= '0' && line[pos] <= '9') {
      seconds = seconds * 10u + (uint64_t)(line[pos++] - '0');
      digits++;
    }
    if (digits == 0) {
      return CANDUMP_LINE_ERROR;
    }
    if (pos < length && line[pos] == '.') {
      pos++;
      size_t fraction_digits = 0;
      while (pos < length && line[pos] >= '0' && line[pos] <= '9') {
        if (fraction_digits < 6) {
          micros = micros * 10u + (uint64_t)(line[pos] - '0');
          fraction_digits++;
        }
        pos++;
      }
      while (fraction_digits++ < 6) {
        micros *= 10u;
      }
    }
    if (pos >= length || line[pos] != ')') {
      return CANDUMP_LINE_ERROR;
    }
    pos++;
    *time_us = seconds * 1000000u + micros;
    if (has_time != NULL) {
      *has_time = true;
    }
    while (pos < length && is_space(line[pos])) {
      pos++;
    }
  }

  /* Optional interface name: a token before the frame that contains no '#'. */
  size_t token_end = pos;
  while (token_end < length && !is_space(line[token_end])) {
    token_end++;
  }
  if (token_end < length && memchr(&line[pos], '#', token_end - pos) == NULL) {
    pos = token_end;
    while (pos < length && is_space(line[pos])) {
      pos++;
    }
  }

  /* The frame, followed by nothing or by candump's direction flag (" R" / " T"). */
  size_t frame_end = pos;
  while (frame_end < length && !is_space(line[frame_end])) {
    frame_end++;
  }
  if (frame_end == pos || !can_frame_from_text(&line[pos], frame_end - pos, frame)) {
    return CANDUMP_LINE_ERROR;
  }
  return CANDUMP_LINE_FRAME;
}
