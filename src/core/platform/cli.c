#include "cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

bool cli_parse_long(const char* text, long min, long max, long* value) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  char* end = NULL;
  errno = 0;
  long parsed = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed < min || parsed > max) {
    return false;
  }
  *value = parsed;
  return true;
}

static const char* next_value(int argc, char** argv, int* index) {
  if (*index + 1 >= argc) {
    fprintf(stderr, "Missing value for %s\n", argv[*index]);
    return NULL;
  }
  (*index)++;
  return argv[*index];
}

int cli_parse_common_option(int argc, char** argv, int* index, PalRtConfig* rt) {
  const char* option = argv[*index];
  long value = 0;

  if (strcmp(option, "--rt-priority") == 0) {
    const char* text = next_value(argc, argv, index);
    if (!text || !cli_parse_long(text, 1, 99, &value)) {
      fprintf(stderr, "--rt-priority expects a value between 1 and 99\n");
      return -1;
    }
    rt->priority = (int)value;
    return 1;
  }
  if (strcmp(option, "--rt-cpu") == 0) {
    const char* text = next_value(argc, argv, index);
    if (!text || !cli_parse_long(text, 0, 1023, &value)) {
      fprintf(stderr, "--rt-cpu expects a CPU index\n");
      return -1;
    }
    rt->cpu = (int)value;
    return 1;
  }
  if (strcmp(option, "--rt-lock-memory") == 0) {
    rt->lock_memory = true;
    return 1;
  }
  if (strcmp(option, "--log-level") == 0) {
    const char* text = next_value(argc, argv, index);
    LogLevel level;
    if (!text || !log_parse_level(text, &level)) {
      fprintf(stderr, "--log-level expects debug, info, warn, error or off\n");
      return -1;
    }
    log_set_level(level);
    return 1;
  }
  return 0;
}

const char* cli_common_usage(void) {
  return "  --log-level LEVEL     debug | info | warn | error | off (default: info)\n"
         "  --rt-priority N       run the main loop with SCHED_FIFO priority N (1..99, Linux)\n"
         "  --rt-cpu N            pin the main loop to CPU core N\n"
         "  --rt-lock-memory      lock all memory pages (mlockall, Linux)\n";
}
