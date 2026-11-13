#ifndef IOTWIN_CANDUMP_LOG_H
#define IOTWIN_CANDUMP_LOG_H

/**
 * @file candump_log.h
 * @brief Log file lines in the can-utils "candump -L" format.
 *
 *   (1700000000.123456) iotwin 123#DEADBEEF
 *
 * Files written by can_monitor --log can be replayed with can_replay or
 * canplayer (can-utils), and candump logs from a real bus can be replayed
 * onto iotwin-bus. On input the timestamp and the interface are optional, so
 * plain "123#DEAD" lines work too; empty lines and lines starting with '#'
 * or ';' are skipped.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANDUMP_LOG_LINE_MAX 96 /**< Buffer size for a formatted line incl. NUL. */

typedef enum {
  CANDUMP_LINE_FRAME, /**< A frame was parsed. */
  CANDUMP_LINE_SKIP,  /**< Empty line or comment. */
  CANDUMP_LINE_ERROR  /**< Not a valid log line. */
} CandumpLineType;

/**
 * @brief Formats "(sec.usec) interface frame" without line break.
 * @return Length, or -1 if the frame is invalid or @p size too small.
 */
int candump_log_format(const CanFrame* frame, uint64_t time_us, const char* interface, char* out, size_t size);

/**
 * @brief Parses one line.
 * @param time_us Receives the timestamp in microseconds (0 if the line has none).
 * @param has_time Optional, set to true if the line had a timestamp.
 */
CandumpLineType candump_log_parse(const char* line, size_t length, CanFrame* frame, uint64_t* time_us,
                                  bool* has_time);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_CANDUMP_LOG_H */
