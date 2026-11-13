#ifndef IOTWIN_CLI_H
#define IOTWIN_CLI_H

/**
 * @file cli.h
 * @brief Helpers shared by the command line front ends.
 */

#include <stdbool.h>

#include "pal_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Parses a decimal integer in [min, max]. Returns false on garbage or overflow. */
bool cli_parse_long(const char* text, long min, long max, long* value);

/**
 * @brief Consumes the real-time and log options (--rt-priority N, --rt-cpu N,
 *        --rt-lock-memory, --log-level L) at argv[*index].
 * @return 1 if the option was consumed (and *index advanced past its value),
 *         0 if argv[*index] is not one of these options,
 *        -1 if the option was recognised but its value is invalid/missing.
 */
int cli_parse_common_option(int argc, char** argv, int* index, PalRtConfig* rt);

/** @brief Usage text for the options handled by cli_parse_common_option(). */
const char* cli_common_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_CLI_H */
