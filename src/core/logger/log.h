#ifndef IOTWIN_LOG_H
#define IOTWIN_LOG_H

/**
 * @file log.h
 * @brief Small thread-safe logger with severity levels and timestamps.
 *
 * All output goes to stderr so that stdout stays free for program output
 * (e.g. the interactive console of a simulator).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Severity levels, ordered from most to least verbose. */
typedef enum {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_OFF
} LogLevel;

/** @brief Sets the minimum level that is written (default: LOG_LEVEL_INFO). */
void log_set_level(LogLevel level);

/** @brief Returns the currently active minimum level. */
LogLevel log_get_level(void);

/**
 * @brief Parses "debug", "info", "warn", "error" or "off" (case-insensitive).
 * @return true on success, false if @p text is not a known level.
 */
bool log_parse_level(const char* text, LogLevel* level);

#if defined(__MINGW32__)
#define IOTWIN_PRINTF_FORMAT(fmt_index, args_index) \
  __attribute__((format(gnu_printf, fmt_index, args_index)))
#elif defined(__GNUC__)
#define IOTWIN_PRINTF_FORMAT(fmt_index, args_index) \
  __attribute__((format(printf, fmt_index, args_index)))
#else
#define IOTWIN_PRINTF_FORMAT(fmt_index, args_index)
#endif

/** @brief Writes one printf-style formatted line if @p level is enabled. */
void log_write(LogLevel level, const char* fmt, ...) IOTWIN_PRINTF_FORMAT(2, 3);

#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_write(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_write(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_LOG_H */
