#ifndef IOTWIN_LINE_READER_H
#define IOTWIN_LINE_READER_H

/**
 * @file line_reader.h
 * @brief Splits a TCP byte stream into newline terminated lines.
 *
 * TCP has no message boundaries: one recv() can contain half a line or
 * several lines. The reader buffers partial input, strips a trailing '\r'
 * ("\r\n" line endings) and skips empty lines. Lines longer than
 * LINE_READER_MAX_LINE - 1 characters are dropped as a whole.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINE_READER_MAX_LINE 256

/** @brief Called for every complete line; @p line is NUL terminated and valid only during the call. */
typedef void (*LineReaderCallback)(const char* line, size_t length, void* user_data);

/** @brief Reader state (one per connection). */
typedef struct {
  char buffer[LINE_READER_MAX_LINE]; /**< Partial line. */
  size_t length;                     /**< Bytes in buffer. */
  bool discarding;                   /**< Current line is too long and being skipped. */
  uint64_t dropped_lines;            /**< Number of over-long lines dropped. */
} LineReader;

void line_reader_init(LineReader* reader);

/**
 * @brief Feeds received bytes into the reader.
 * @return Number of lines passed to @p callback.
 */
size_t line_reader_feed(LineReader* reader, const char* data, size_t length,
                        LineReaderCallback callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_LINE_READER_H */
