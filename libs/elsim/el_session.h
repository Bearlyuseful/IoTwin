#ifndef IOTWIN_EL_SESSION_H
#define IOTWIN_EL_SESSION_H

/**
 * @file el_session.h
 * @brief Transport-independent interface of the electrical simulation (ADR 0008).
 *
 * A session runs one installation: **commands** go in, **snapshots** (the
 * complete electrical state after a step) and **events** (trips, rejected
 * commands) come out. Every adapter - in-process (the Unreal visualizer),
 * shared memory or network - only moves these three things.
 *
 * Threads: one thread sends commands, one simulation thread calls
 * el_session_step(), one reader thread takes snapshots and events (sender and
 * reader may be the same thread). The queues and the snapshot exchange are
 * lock-free and wait-free, so a real-time simulation thread never waits for a
 * slow reader such as a game engine frame; the reader always gets the newest
 * complete snapshot. The simulation thread does not allocate memory after the
 * first step.
 *
 * The topology (devices, terminals, connections) is fixed for the lifetime of
 * a session; a changed installation means a new session.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elsim.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EL_SESSION_QUEUE 256 /**< commands and events in flight */

typedef enum {
  EL_CMD_SET_ON,       /**< target: device, value: 1 on / 0 off */
  EL_CMD_RESET,        /**< target: breaker, fuse or RCD */
  EL_CMD_PRESS_TEST,   /**< target: RCD */
  EL_CMD_SET_LEVEL,    /**< target: dimmer, value: 0..1 */
  EL_CMD_ADD_FAULT,    /**< target, other: terminals "device:terminal", value: Ohm */
  EL_CMD_CLEAR_FAULTS, /**< removes all faults and interruptions */
  EL_CMD_SET_OPEN,     /**< target: connection, value: 1 interrupted / 0 restored */
  EL_CMD_PLUG          /**< target: load, other: socket ("" = unplug) */
} ElCommandType;

typedef struct {
  ElCommandType type;
  double value;
  char target[EL_ID_MAX];
  char other[EL_ID_MAX];
} ElCommand;

/** @brief Fills a command (target/other may be NULL). */
ElCommand el_command(ElCommandType type, const char* target, const char* other, double value);

#define EL_STATE_ON 0x01u      /**< handle/switch on (load: its own switch) */
#define EL_STATE_TRIPPED 0x02u /**< breaker/RCD tripped, fuse blown */
#define EL_STATE_POWERED 0x04u /**< load supplied (el_device_powered) */

/** State of one device after a step. */
typedef struct {
  double voltage_v;  /**< load, socket, junction: L-N */
  double current_a;  /**< see ElDevice.current_a */
  double power_w;    /**< load */
  double residual_a; /**< RCD */
  double level;      /**< dimmer */
  uint32_t flags;    /**< EL_STATE_* */
} ElDeviceState;

/** Complete electrical state after a step. Arrays are indexed like ElModel.devices / connections. */
typedef struct {
  uint64_t step;     /**< steps since the start; 0 = nothing computed yet */
  double time_s;     /**< simulated time */
  bool solved;       /**< false if the network could not be solved */
  size_t device_count;
  size_t connection_count;
  ElDeviceState* devices;
  double* connection_current_a;
} ElSnapshot;

typedef struct ElSession ElSession;

/**
 * @brief Starts a session and takes over the model (it is moved: the caller's
 *        struct is left empty and must not be used except for el_model_init).
 * @return NULL if memory runs out (the model is freed then).
 */
ElSession* el_session_create(ElModel* model);
void el_session_destroy(ElSession* session);

/** @name Topology (any thread; ids, types and terminals do not change) */
/**@{*/
size_t el_session_device_count(const ElSession* session);
int el_session_find_device(const ElSession* session, const char* id);
const char* el_session_device_id(const ElSession* session, size_t index);
ElDeviceType el_session_device_type(const ElSession* session, size_t index);
/** @brief Rated power of a load (W) - a fixed parameter. */
double el_session_rated_power(const ElSession* session, size_t index);
/**@}*/

/** @brief Command thread: queues a command for the next step. @return false if the queue is full. */
bool el_session_send(ElSession* session, const ElCommand* command);

/**
 * @brief Simulation thread: applies the queued commands, advances the time by
 *        @p dt_s, publishes a snapshot and the events of this step.
 * @return false if the network could not be solved.
 */
bool el_session_step(ElSession* session, double dt_s);

/** @brief Reader thread: newest complete snapshot; valid until the next call. */
const ElSnapshot* el_session_snapshot(ElSession* session);

/** @brief Reader thread: oldest unread event. */
bool el_session_next_event(ElSession* session, ElEvent* event);

/** @brief Events lost because the reader did not keep up. */
unsigned long el_session_dropped_events(ElSession* session);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_SESSION_H */
