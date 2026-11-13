#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "log.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static atomic_int g_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void log_set_level(LogLevel level) { atomic_store(&g_level, (int)level); }

LogLevel log_get_level(void) { return (LogLevel)atomic_load(&g_level); }

static bool equals_ignore_case(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return false;
    }
    a++;
    b++;
  }
  return *a == *b;
}

bool log_parse_level(const char* text, LogLevel* level) {
  static const struct {
    const char* name;
    LogLevel level;
  } names[] = {
      {"debug", LOG_LEVEL_DEBUG}, {"info", LOG_LEVEL_INFO},   {"warn", LOG_LEVEL_WARN},
      {"warning", LOG_LEVEL_WARN}, {"error", LOG_LEVEL_ERROR}, {"off", LOG_LEVEL_OFF},
  };
  if (text == NULL || level == NULL) {
    return false;
  }
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (equals_ignore_case(text, names[i].name)) {
      *level = names[i].level;
      return true;
    }
  }
  return false;
}

static const char* level_name(LogLevel level) {
  switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO: return "INFO ";
    case LOG_LEVEL_WARN: return "WARN ";
    case LOG_LEVEL_ERROR: return "ERROR";
    default: return "?????";
  }
}

static void format_timestamp(char* buffer, size_t size) {
#ifdef _WIN32
  SYSTEMTIME now;
  GetLocalTime(&now);
  snprintf(buffer, size, "%02u:%02u:%02u.%03u", (unsigned)now.wHour, (unsigned)now.wMinute,
           (unsigned)now.wSecond, (unsigned)now.wMilliseconds);
#else
  struct timespec now;
  struct tm local;
  clock_gettime(CLOCK_REALTIME, &now);
  localtime_r(&now.tv_sec, &local);
  snprintf(buffer, size, "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min, local.tm_sec,
           now.tv_nsec / 1000000L);
#endif
}

void log_write(LogLevel level, const char* fmt, ...) {
  if (level >= LOG_LEVEL_OFF || (int)level < atomic_load(&g_level)) {
    return;
  }

  char message[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  char stamp[32];
  format_timestamp(stamp, sizeof(stamp));

  pthread_mutex_lock(&g_lock);
  fprintf(stderr, "%s [%s] %s\n", stamp, level_name(level), message);
  fflush(stderr);
  pthread_mutex_unlock(&g_lock);
}
