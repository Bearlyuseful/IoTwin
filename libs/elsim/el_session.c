#include "el_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el_atomic.h"

/*
 * Queues: single producer, single consumer rings. Positions run modulo
 * 2 x capacity, so "full" and "empty" can be told apart without a counter.
 *
 * Snapshots: triple buffer. The simulation thread fills "back" and exchanges
 * it with "middle" (marked fresh); the reader exchanges "front" with "middle"
 * when it is fresh. Neither side ever waits.
 */

#define RING (2L * EL_SESSION_QUEUE)
#define FRESH 0x4L
#define INDEX_MASK 0x3L

struct ElSession {
  ElModel model;
  ElSnapshot buffers[3];
  ElAtomic middle; /* buffer index | FRESH */
  long back;       /* simulation thread only */
  long front;      /* reader thread only */
  uint64_t steps;

  ElCommand commands[EL_SESSION_QUEUE];
  ElAtomic command_head; /* advanced by the simulation thread */
  ElAtomic command_tail; /* advanced by the command thread */

  ElEvent events[EL_SESSION_QUEUE];
  ElAtomic event_head; /* advanced by the reader */
  ElAtomic event_tail; /* advanced by the simulation thread */
  ElAtomic dropped_events;
};

static void copy_text(char* out, size_t size, const char* text) { snprintf(out, size, "%s", text ? text : ""); }

ElCommand el_command(ElCommandType type, const char* target, const char* other, double value) {
  ElCommand command;
  memset(&command, 0, sizeof(command));
  command.type = type;
  command.value = value;
  copy_text(command.target, sizeof(command.target), target);
  copy_text(command.other, sizeof(command.other), other);
  return command;
}

static long ring_count(long head, long tail) { return (tail - head + RING) % RING; }

static bool alloc_snapshot(ElSnapshot* s, size_t devices, size_t connections) {
  memset(s, 0, sizeof(*s));
  s->device_count = devices;
  s->connection_count = connections;
  s->devices = (ElDeviceState*)calloc(devices ? devices : 1, sizeof(ElDeviceState));
  s->connection_current_a = (double*)calloc(connections ? connections : 1, sizeof(double));
  return s->devices != NULL && s->connection_current_a != NULL;
}

ElSession* el_session_create(ElModel* model) {
  ElSession* session = (ElSession*)calloc(1, sizeof(ElSession));
  if (session == NULL) {
    el_model_free(model);
    return NULL;
  }
  session->model = *model;
  memset(model, 0, sizeof(*model));
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    ok = alloc_snapshot(&session->buffers[i], session->model.device_count, session->model.connection_count) && ok;
  }
  if (!ok) {
    el_session_destroy(session);
    return NULL;
  }
  session->back = 0;
  session->middle = 1;
  session->front = 2;
  return session;
}

void el_session_destroy(ElSession* session) {
  if (session == NULL) {
    return;
  }
  for (int i = 0; i < 3; i++) {
    free(session->buffers[i].devices);
    free(session->buffers[i].connection_current_a);
  }
  el_model_free(&session->model);
  free(session);
}

size_t el_session_device_count(const ElSession* session) { return session->model.device_count; }

int el_session_find_device(const ElSession* session, const char* id) {
  return el_model_find_device(&session->model, id);
}

const char* el_session_device_id(const ElSession* session, size_t index) {
  return index < session->model.device_count ? session->model.devices[index].id : NULL;
}

ElDeviceType el_session_device_type(const ElSession* session, size_t index) {
  return index < session->model.device_count ? session->model.devices[index].type : EL_DEVICE_TYPES;
}

double el_session_rated_power(const ElSession* session, size_t index) {
  return index < session->model.device_count ? session->model.devices[index].rated_power_w : 0.0;
}

/* ------------------------------------------------------------------------- */
/* Commands (command thread -> simulation thread)                            */
/* ------------------------------------------------------------------------- */

bool el_session_send(ElSession* session, const ElCommand* command) {
  const long tail = el_atomic_load(&session->command_tail);
  const long head = el_atomic_load(&session->command_head);
  if (ring_count(head, tail) >= EL_SESSION_QUEUE) {
    return false;
  }
  session->commands[tail % EL_SESSION_QUEUE] = *command;
  el_atomic_store(&session->command_tail, (tail + 1) % RING);
  return true;
}

static bool next_command(ElSession* session, ElCommand* command) {
  const long head = el_atomic_load(&session->command_head);
  const long tail = el_atomic_load(&session->command_tail);
  if (head == tail) {
    return false;
  }
  *command = session->commands[head % EL_SESSION_QUEUE];
  el_atomic_store(&session->command_head, (head + 1) % RING);
  return true;
}

/* ------------------------------------------------------------------------- */
/* Events (simulation thread -> reader)                                      */
/* ------------------------------------------------------------------------- */

static void publish_event(ElSession* session, const ElEvent* event) {
  const long tail = el_atomic_load(&session->event_tail);
  const long head = el_atomic_load(&session->event_head);
  if (ring_count(head, tail) >= EL_SESSION_QUEUE) {
    el_atomic_store(&session->dropped_events, el_atomic_load(&session->dropped_events) + 1);
    return;
  }
  session->events[tail % EL_SESSION_QUEUE] = *event;
  el_atomic_store(&session->event_tail, (tail + 1) % RING);
}

bool el_session_next_event(ElSession* session, ElEvent* event) {
  const long head = el_atomic_load(&session->event_head);
  const long tail = el_atomic_load(&session->event_tail);
  if (head == tail) {
    return false;
  }
  *event = session->events[head % EL_SESSION_QUEUE];
  el_atomic_store(&session->event_head, (head + 1) % RING);
  return true;
}

unsigned long el_session_dropped_events(ElSession* session) {
  return (unsigned long)el_atomic_load(&session->dropped_events);
}

/* ------------------------------------------------------------------------- */
/* Simulation thread                                                         */
/* ------------------------------------------------------------------------- */

static const char* command_name(ElCommandType type) {
  switch (type) {
    case EL_CMD_SET_ON:
      return "set_on";
    case EL_CMD_RESET:
      return "reset";
    case EL_CMD_PRESS_TEST:
      return "test";
    case EL_CMD_SET_LEVEL:
      return "level";
    case EL_CMD_ADD_FAULT:
      return "fault";
    case EL_CMD_CLEAR_FAULTS:
      return "clear";
    case EL_CMD_SET_OPEN:
      return "open";
    case EL_CMD_PLUG:
      return "plug";
  }
  return "?";
}

static void apply(ElSession* session, const ElCommand* c) {
  ElModel* m = &session->model;
  bool ok = false;
  switch (c->type) {
    case EL_CMD_SET_ON:
      ok = el_model_set_on(m, c->target, c->value != 0.0);
      break;
    case EL_CMD_RESET:
      ok = el_model_reset(m, c->target);
      break;
    case EL_CMD_PRESS_TEST:
      ok = el_model_press_test(m, c->target);
      break;
    case EL_CMD_SET_LEVEL:
      ok = el_model_set_level(m, c->target, c->value);
      break;
    case EL_CMD_ADD_FAULT:
      ok = el_model_add_fault(m, c->target, c->other, c->value, NULL) >= 0;
      break;
    case EL_CMD_CLEAR_FAULTS:
      el_model_clear_faults(m);
      ok = true;
      break;
    case EL_CMD_SET_OPEN:
      ok = el_model_set_connection_open(m, c->target, c->value != 0.0);
      break;
    case EL_CMD_PLUG:
      ok = el_model_plug(m, c->target, c->other[0] ? c->other : NULL);
      break;
  }
  if (!ok) {
    ElEvent event;
    event.time_s = m->time_s;
    snprintf(event.text, sizeof(event.text), "command rejected: %s %s %s", command_name(c->type), c->target, c->other);
    publish_event(session, &event);
  }
}

static void fill_snapshot(ElSession* session, ElSnapshot* s, bool solved) {
  const ElModel* m = &session->model;
  s->step = session->steps;
  s->time_s = m->time_s;
  s->solved = solved;
  for (size_t i = 0; i < m->device_count; i++) {
    const ElDevice* d = &m->devices[i];
    ElDeviceState* state = &s->devices[i];
    state->voltage_v = d->voltage_v_measured;
    state->current_a = d->current_a;
    state->power_w = d->power_w;
    state->residual_a = d->residual_a;
    state->level = d->level;
    state->flags = (d->on ? EL_STATE_ON : 0u) | (d->tripped ? EL_STATE_TRIPPED : 0u) |
                   (el_device_powered(d) ? EL_STATE_POWERED : 0u);
  }
  for (size_t i = 0; i < m->connection_count; i++) {
    s->connection_current_a[i] = m->connections[i].current_a;
  }
}

bool el_session_step(ElSession* session, double dt_s) {
  ElCommand command;
  while (next_command(session, &command)) {
    apply(session, &command);
  }
  const bool ok = el_model_step(&session->model, dt_s);
  session->steps++;
  ElEvent event;
  while (el_model_next_event(&session->model, &event)) {
    publish_event(session, &event);
  }
  fill_snapshot(session, &session->buffers[session->back], ok);
  session->back = el_atomic_exchange(&session->middle, session->back | FRESH) & INDEX_MASK;
  return ok;
}

const ElSnapshot* el_session_snapshot(ElSession* session) {
  if (el_atomic_load(&session->middle) & FRESH) {
    session->front = el_atomic_exchange(&session->middle, session->front) & INDEX_MASK;
  }
  return &session->buffers[session->front];
}
