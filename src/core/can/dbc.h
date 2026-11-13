#ifndef IOTWIN_DBC_H
#define IOTWIN_DBC_H

/**
 * @file dbc.h
 * @brief Reader for the DBC subset needed to name CAN traffic.
 *
 * Supported: BO_ (messages, extended ids via bit 31), SG_ (signals incl.
 * multiplexer "M" and multiplexed "mN" signals), VAL_ (value descriptions).
 * Everything else (BU_, CM_, BA_, NS_, ...) is skipped. Host only (heap).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"
#include "can_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DBC_NAME_MAX 64
#define DBC_UNIT_MAX 16

typedef struct {
  int64_t value;
  char text[DBC_NAME_MAX];
} DbcValueName;

typedef struct {
  char name[DBC_NAME_MAX];
  CanSignal signal;
  double minimum;
  double maximum;
  char unit[DBC_UNIT_MAX];
  bool is_multiplexer;   /**< "M": selects which multiplexed signals are present */
  bool is_multiplexed;   /**< "mN": only present if the multiplexer equals mux_value */
  uint32_t mux_value;
  DbcValueName* values;
  size_t value_count;
} DbcSignal;

typedef struct {
  uint32_t id;
  bool extended;
  char name[DBC_NAME_MAX];
  uint8_t dlc;
  char transmitter[DBC_NAME_MAX];
  DbcSignal* signals;
  size_t signal_count;
} DbcMessage;

typedef struct {
  DbcMessage* messages;
  size_t message_count;
} DbcDatabase;

/**
 * @brief Parses DBC text.
 * @param error Receives "line N: reason" on failure.
 */
bool dbc_parse(const char* text, DbcDatabase* database, char* error, size_t error_size);

/** @brief Reads and parses a file. */
bool dbc_load_file(const char* path, DbcDatabase* database, char* error, size_t error_size);

void dbc_free(DbcDatabase* database);

const DbcMessage* dbc_find_message(const DbcDatabase* database, uint32_t id, bool extended);
const DbcMessage* dbc_find_message_by_name(const DbcDatabase* database, const char* name);
const DbcSignal* dbc_find_signal(const DbcMessage* message, const char* name);

/** @brief Value description for a raw value, NULL if none. */
const char* dbc_value_name(const DbcSignal* signal, int64_t raw);

/** @brief True if the signal is present in this payload (multiplexing). */
bool dbc_signal_present(const DbcMessage* message, const DbcSignal* signal, const uint8_t data[8]);

/**
 * @brief Human readable decoding, e.g. "LAMP_CMD LampOn=1 (On)".
 * @return Characters written, or -1 if the message is unknown.
 */
int dbc_describe_frame(const DbcDatabase* database, const CanFrame* frame, char* out, size_t out_size);

/** @brief Sets a signal by physical value in @p frame (id/dlc taken from the message). */
bool dbc_set_signal(const DbcMessage* message, const char* signal_name, double physical, CanFrame* frame);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_DBC_H */
