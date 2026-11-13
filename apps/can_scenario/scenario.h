#ifndef IOTWIN_SCENARIO_H
#define IOTWIN_SCENARIO_H

/**
 * @file scenario.h
 * @brief Scenario scripts for can_scenario (see docs/dev/scenarios.md).
 *
 * One step per line, '#' starts a comment:
 *
 *   name Lamp follows the wall switch
 *   send 200#01                      put a frame on the bus
 *   expect 100#01 within 300         a matching frame must arrive (default 1000 ms)
 *   expect-not 100#01 for 500        no matching frame for 500 ms
 *   wait 200                         let time pass (ms)
 *   bench ecu fault short on         test bench command to node "ecu" (can_sim)
 *   bus drop 100 200                 fault of the bus: drop | delay | silence | seed | clear
 *   uds ecu 190209 expect 59027F900111*   UDS request (ecu, gateway or TX:RX ids)
 *   log any text                     printed while running
 *
 * Frame patterns: "100#01" exact, "100#01*" payload starting with 01,
 * "100#*" any payload, "100#XX01" XX = any byte, "100#R" remote frame.
 * UDS expectations are hex bytes, "*" at the end allows more bytes; without
 * "expect" a positive response is required.
 *
 * Parsing is free of I/O so it is unit tested; main_can_scenario.c runs it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCENARIO_MAX_STEPS 256
#define SCENARIO_TEXT_MAX 160
#define SCENARIO_UDS_MAX 64
#define SCENARIO_DEFAULT_TIMEOUT_MS 1000u

typedef enum {
  STEP_SEND,
  STEP_EXPECT,
  STEP_EXPECT_NOT,
  STEP_WAIT,
  STEP_BENCH,
  STEP_BUS,
  STEP_UDS,
  STEP_LOG
} ScenarioStepType;

typedef struct {
  uint32_t id;
  bool extended;
  bool rtr;
  bool any_payload;   /* "#*" */
  bool prefix;        /* trailing '*': at least these bytes, more allowed */
  uint8_t length;     /* number of payload bytes in the pattern */
  uint8_t data[CAN_MAX_DLEN];
  uint8_t wildcard;   /* bit i set: byte i is "XX" */
} FramePattern;

typedef struct {
  ScenarioStepType type;
  int line;
  uint32_t ms;                    /* expect: within, expect-not: for, wait */
  CanFrame frame;                 /* send */
  FramePattern pattern;           /* expect, expect-not */
  char text[SCENARIO_TEXT_MAX];   /* bench: "@target cmd", bus: "fault ...", log, source text of the step */
  uint32_t uds_tx;
  uint32_t uds_rx;
  uint8_t uds_request[SCENARIO_UDS_MAX];
  size_t uds_request_length;
  bool uds_expect_given;
  bool uds_expect_prefix;
  uint8_t uds_expect[SCENARIO_UDS_MAX];
  size_t uds_expect_length;
} ScenarioStep;

typedef struct {
  char name[SCENARIO_TEXT_MAX];
  ScenarioStep steps[SCENARIO_MAX_STEPS];
  size_t count;
} Scenario;

void scenario_init(Scenario* scenario);

/** @brief Adds the step in @p line (comments and empty lines are accepted). */
bool scenario_parse_line(Scenario* scenario, const char* line, int line_number, char* error, size_t error_size);

/** @brief Loads a file; the name defaults to the file name. */
bool scenario_load(Scenario* scenario, const char* path, char* error, size_t error_size);

bool frame_pattern_parse(const char* text, FramePattern* pattern);
bool frame_pattern_matches(const FramePattern* pattern, const CanFrame* frame);

/** @brief Hex bytes "22F190" (spaces and dots allowed); returns the count or -1. */
int scenario_parse_hex(const char* text, uint8_t* out, size_t capacity, bool* trailing_star);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_SCENARIO_H */
