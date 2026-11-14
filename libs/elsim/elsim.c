#include "elsim.h"

#include <complex.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUPPLY_INTERNAL_NODES 4 /* star point S and three source nodes E1..E3 */
#define RCD_TRIP_FRACTION 0.8   /* trips between 0.5 and 1.0 x I delta n, here at 0.8 */
#define RCD_TEST_MULTIPLE 2.0   /* test button draws about twice I delta n */
#define PLUG_CONTACT_OHM 0.005
#define BREAKER_COOLING 1.0     /* normalised heat units per second below In */
#define MAX_TRIP_ITERATIONS 8

static const char* const kTypeNames[EL_DEVICE_TYPES] = {"supply", "breaker", "fuse", "rcd", "switch", "load", "socket"};

static const char* const kTerminals[EL_DEVICE_TYPES][EL_MAX_TERMINALS] = {
    {"L1", "L2", "L3", "N", "PE"},
    {"in", "out"},
    {"in", "out"},
    {"L_in", "N_in", "L_out", "N_out"},
    {"in", "out"},
    {"L", "N", "PE"},
    {"L", "N", "PE"},
};
static const int kTerminalCount[EL_DEVICE_TYPES] = {5, 2, 2, 4, 2, 3, 3};

enum { SUPPLY_L1, SUPPLY_L2, SUPPLY_L3, SUPPLY_N, SUPPLY_PE };
enum { TWO_IN, TWO_OUT };
enum { RCD_L_IN, RCD_N_IN, RCD_L_OUT, RCD_N_OUT };
enum { LOAD_L, LOAD_N, LOAD_PE };

const char* el_device_type_name(ElDeviceType type) {
  return (type >= 0 && type < EL_DEVICE_TYPES) ? kTypeNames[type] : "unknown";
}

bool el_device_type_from_name(const char* name, ElDeviceType* type) {
  for (int i = 0; i < EL_DEVICE_TYPES; i++) {
    if (strcmp(name, kTypeNames[i]) == 0) {
      *type = (ElDeviceType)i;
      return true;
    }
  }
  return false;
}

int el_terminal_count(ElDeviceType type) { return (type >= 0 && type < EL_DEVICE_TYPES) ? kTerminalCount[type] : 0; }

const char* el_terminal_name(ElDeviceType type, int terminal) {
  return (terminal >= 0 && terminal < el_terminal_count(type)) ? kTerminals[type][terminal] : NULL;
}

bool el_device_powered(const ElDevice* device) {
  return device->type == EL_LOAD && device->voltage_v_measured >= 0.5 * device->voltage_v;
}

/* ------------------------------------------------------------------------- */
/* Building                                                                   */
/* ------------------------------------------------------------------------- */

void el_model_init(ElModel* model) {
  memset(model, 0, sizeof(*model));
  model->node_count = 1; /* node 0 = earth */
  el_circuit_init(&model->circuit, 1);
}

void el_model_free(ElModel* model) {
  free(model->devices);
  free(model->cables);
  free(model->connections);
  free(model->faults);
  el_circuit_free(&model->circuit);
  memset(model, 0, sizeof(*model));
}

static void copy_id(char* out, size_t size, const char* text) { snprintf(out, size, "%s", text ? text : ""); }

/* Grows an array of fixed-size records by one; returns the new element or NULL. */
static void* grow(void** items, size_t* count, size_t size) {
  void* grown = realloc(*items, (*count + 1) * size);
  if (grown == NULL) {
    return NULL;
  }
  *items = grown;
  void* item = (char*)grown + *count * size;
  memset(item, 0, size);
  (*count)++;
  return item;
}

int el_model_find_device(const ElModel* model, const char* id) {
  for (size_t i = 0; i < model->device_count; i++) {
    if (strcmp(model->devices[i].id, id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

int el_model_find_connection(const ElModel* model, const char* id) {
  for (size_t i = 0; i < model->connection_count; i++) {
    if (strcmp(model->connections[i].id, id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static int find_cable(const ElModel* model, const char* id) {
  for (size_t i = 0; i < model->cable_count; i++) {
    if (strcmp(model->cables[i].id, id) == 0) {
      return (int)i;
    }
  }
  return -1;
}

static void set_defaults(ElDevice* d) {
  d->plugged_into = -1;
  d->on = d->type != EL_SWITCH; /* switches start open, everything else switched on */
  for (int i = 0; i < 3; i++) {
    d->element[i] = -1;
  }
  switch (d->type) {
    case EL_SUPPLY:
      d->voltage_v = 230.0;
      d->frequency_hz = 50.0;
      d->loop_impedance_ohm = 0.35;
      d->earthing = EL_TN_S;
      d->earth_electrode_ohm = 30.0;
      d->source_earth_ohm = 1.0;
      break;
    case EL_BREAKER:
      d->rated_current_a = 16.0;
      d->magnetic_multiple = 5.0; /* characteristic B: 3..5 In */
      d->thermal_limit = 60.0;
      d->contact_ohm = 0.002;
      break;
    case EL_FUSE:
      d->rated_current_a = 16.0;
      d->thermal_limit = 1000.0; /* A^2 s above the rating */
      d->contact_ohm = 0.003;
      break;
    case EL_RCD:
      d->rated_current_a = 40.0;
      d->residual_rated_a = 0.03;
      d->contact_ohm = 0.002;
      break;
    case EL_SWITCH:
      d->rated_current_a = 10.0;
      d->contact_ohm = 0.005;
      break;
    case EL_LOAD:
      d->voltage_v = 230.0;
      d->rated_power_w = 100.0;
      break;
    case EL_SOCKET:
    case EL_DEVICE_TYPES:
      break;
  }
}

int el_model_add_device(ElModel* model, const char* id, ElDeviceType type) {
  if (type < 0 || type >= EL_DEVICE_TYPES || el_model_find_device(model, id) >= 0) {
    return -1;
  }
  ElDevice* d = grow((void**)&model->devices, &model->device_count, sizeof(ElDevice));
  if (d == NULL) {
    return -1;
  }
  copy_id(d->id, sizeof(d->id), id);
  d->type = type;
  d->first_node = model->node_count;
  model->node_count += kTerminalCount[type];
  d->internal_node = -1;
  if (type == EL_SUPPLY) {
    d->internal_node = model->node_count;
    model->node_count += SUPPLY_INTERNAL_NODES;
  }
  set_defaults(d);
  return (int)model->device_count - 1;
}

int el_model_add_cable(ElModel* model, const char* id, const char* type, double cross_section_mm2, double length_m) {
  if (find_cable(model, id) >= 0 || cross_section_mm2 <= 0.0 || length_m < 0.0) {
    return -1;
  }
  ElCable* c = grow((void**)&model->cables, &model->cable_count, sizeof(ElCable));
  if (c == NULL) {
    return -1;
  }
  copy_id(c->id, sizeof(c->id), id);
  copy_id(c->type, sizeof(c->type), type);
  c->cross_section_mm2 = cross_section_mm2;
  c->length_m = length_m;
  return (int)model->cable_count - 1;
}

int el_model_terminal_node(const ElModel* model, const char* reference) {
  const char* colon = reference ? strchr(reference, ':') : NULL;
  if (colon == NULL) {
    return -1;
  }
  char device_id[EL_ID_MAX];
  const size_t length = (size_t)(colon - reference);
  if (length == 0 || length >= sizeof(device_id)) {
    return -1;
  }
  memcpy(device_id, reference, length);
  device_id[length] = '\0';
  const int index = el_model_find_device(model, device_id);
  if (index < 0) {
    return -1;
  }
  const ElDevice* d = &model->devices[index];
  for (int t = 0; t < kTerminalCount[d->type]; t++) {
    if (strcmp(kTerminals[d->type][t], colon + 1) == 0) {
      return d->first_node + t;
    }
  }
  return -1;
}

int el_model_connect(ElModel* model, const char* id, const char* from, const char* to, const char* role,
                     const char* cable) {
  const int a = el_model_terminal_node(model, from);
  const int b = el_model_terminal_node(model, to);
  const int cable_index = (cable != NULL && cable[0] != '\0') ? find_cable(model, cable) : -1;
  if (a < 0 || b < 0 || a == b || (cable != NULL && cable[0] != '\0' && cable_index < 0)) {
    return -1;
  }
  char generated[EL_ID_MAX];
  if (id == NULL || id[0] == '\0') {
    snprintf(generated, sizeof(generated), "c%zu", model->connection_count + 1);
    id = generated;
  }
  if (el_model_find_connection(model, id) >= 0) {
    return -1;
  }
  ElConnection* c = grow((void**)&model->connections, &model->connection_count, sizeof(ElConnection));
  if (c == NULL) {
    return -1;
  }
  copy_id(c->id, sizeof(c->id), id);
  copy_id(c->role, sizeof(c->role), role);
  c->a = a;
  c->b = b;
  c->cable = cable_index;
  c->element = -1;
  if (cable_index >= 0) {
    const ElCable* k = &model->cables[cable_index];
    c->resistance_ohm = EL_COPPER_RESISTIVITY * k->length_m / k->cross_section_mm2;
  } else {
    c->resistance_ohm = EL_LINK_OHM;
  }
  return (int)model->connection_count - 1;
}

bool el_model_plug(ElModel* model, const char* load, const char* socket) {
  const int l = el_model_find_device(model, load);
  if (l < 0 || model->devices[l].type != EL_LOAD) {
    return false;
  }
  if (socket == NULL) {
    model->devices[l].plugged_into = -1;
    return true;
  }
  const int s = el_model_find_device(model, socket);
  if (s < 0 || model->devices[s].type != EL_SOCKET) {
    return false;
  }
  model->devices[l].plugged_into = s;
  return true;
}

/* ------------------------------------------------------------------------- */
/* Operation                                                                  */
/* ------------------------------------------------------------------------- */

static void push_event(ElModel* model, const char* format, ...) {
  ElEvent* e = &model->events[(model->event_head + model->event_count) % EL_MAX_EVENTS];
  if (model->event_count == EL_MAX_EVENTS) {
    model->event_head = (model->event_head + 1) % EL_MAX_EVENTS; /* drop the oldest */
  } else {
    model->event_count++;
  }
  e->time_s = model->time_s;
  va_list args;
  va_start(args, format);
  vsnprintf(e->text, sizeof(e->text), format, args);
  va_end(args);
}

bool el_model_next_event(ElModel* model, ElEvent* event) {
  if (model->event_count == 0) {
    return false;
  }
  *event = model->events[model->event_head];
  model->event_head = (model->event_head + 1) % EL_MAX_EVENTS;
  model->event_count--;
  return true;
}

bool el_model_set_on(ElModel* model, const char* id, bool on) {
  const int index = el_model_find_device(model, id);
  if (index < 0) {
    return false;
  }
  ElDevice* d = &model->devices[index];
  if (d->type == EL_SUPPLY || d->type == EL_SOCKET) {
    return false;
  }
  if (d->type == EL_FUSE && on && d->tripped) {
    return false; /* a blown fuse has to be replaced */
  }
  d->on = on;
  if (on && (d->type == EL_BREAKER || d->type == EL_RCD)) {
    d->tripped = false; /* switching the handle back on re-arms it */
  }
  return true;
}

bool el_model_reset(ElModel* model, const char* id) {
  const int index = el_model_find_device(model, id);
  if (index < 0) {
    return false;
  }
  ElDevice* d = &model->devices[index];
  if (d->type != EL_BREAKER && d->type != EL_FUSE && d->type != EL_RCD) {
    return false;
  }
  d->tripped = false;
  d->heat = 0.0;
  d->on = true;
  return true;
}

bool el_model_press_test(ElModel* model, const char* id) {
  const int index = el_model_find_device(model, id);
  if (index < 0 || model->devices[index].type != EL_RCD) {
    return false;
  }
  model->devices[index].test_pressed = true;
  return true;
}

int el_model_add_fault(ElModel* model, const char* a, const char* b, double resistance_ohm, const char* description) {
  const int na = el_model_terminal_node(model, a);
  const int nb = el_model_terminal_node(model, b);
  if (na < 0 || nb < 0 || na == nb || resistance_ohm < 0.0) {
    return -1;
  }
  ElFault* f = grow((void**)&model->faults, &model->fault_count, sizeof(ElFault));
  if (f == NULL) {
    return -1;
  }
  f->a = na;
  f->b = nb;
  f->resistance_ohm = resistance_ohm;
  f->element = -1;
  if (description != NULL && description[0] != '\0') {
    copy_id(f->description, sizeof(f->description), description);
  } else {
    snprintf(f->description, sizeof(f->description), "%s - %s, %g Ohm", a, b, resistance_ohm);
  }
  return (int)model->fault_count - 1;
}

bool el_model_set_connection_open(ElModel* model, const char* id, bool open) {
  const int index = el_model_find_connection(model, id);
  if (index < 0) {
    return false;
  }
  model->connections[index].open = open;
  return true;
}

void el_model_clear_faults(ElModel* model) {
  model->fault_count = 0;
  for (size_t i = 0; i < model->connection_count; i++) {
    model->connections[i].open = false;
  }
}

/* ------------------------------------------------------------------------- */
/* Network                                                                    */
/* ------------------------------------------------------------------------- */

static bool conducting(const ElDevice* d) { return d->on && !d->tripped; }

static double load_ohm(const ElDevice* d) {
  return d->rated_power_w > 0.0 ? d->voltage_v * d->voltage_v / d->rated_power_w : 1e12;
}

/*
 * Builds the network of the present state. With zero_sources all voltage
 * sources are 0 V (for Thevenin measurements); inject_a/inject_b >= 0 add a
 * 1 A test current that enters node a and leaves node b.
 */
static bool build_and_solve(ElModel* model, bool zero_sources, int inject_a, int inject_b) {
  ElCircuit* c = &model->circuit;
  if (c->nodes != model->node_count) {
    el_circuit_free(c);
    el_circuit_init(c, model->node_count);
  }
  el_circuit_clear(c);

  for (size_t i = 0; i < model->connection_count; i++) {
    ElConnection* k = &model->connections[i];
    k->element = k->open ? -1 : el_circuit_add_impedance(c, k->a, k->b, k->resistance_ohm);
  }
  for (size_t i = 0; i < model->fault_count; i++) {
    ElFault* f = &model->faults[i];
    f->element = el_circuit_add_impedance(c, f->a, f->b, f->resistance_ohm);
  }

  for (size_t i = 0; i < model->device_count; i++) {
    ElDevice* d = &model->devices[i];
    const int t = d->first_node;
    d->element[0] = d->element[1] = d->element[2] = -1;
    switch (d->type) {
      case EL_SUPPLY: {
        const int star = d->internal_node;
        const double half = d->loop_impedance_ohm / 2.0;
        for (int phase = 0; phase < 3; phase++) {
          const int source = d->internal_node + 1 + phase;
          const ElPhasor u = zero_sources ? 0 : el_polar(d->voltage_v, -120.0 * phase);
          el_circuit_add_voltage_source(c, source, star, u);
          const int element = el_circuit_add_impedance(c, source, t + SUPPLY_L1 + phase, half);
          if (phase == 0) {
            d->element[0] = element;
          }
        }
        d->element[1] = el_circuit_add_impedance(c, t + SUPPLY_N, star, half);
        el_circuit_add_impedance(c, star, 0, d->source_earth_ohm);
        if (d->earthing == EL_TT) {
          d->element[2] = el_circuit_add_impedance(c, t + SUPPLY_PE, 0, d->earth_electrode_ohm);
        } else {
          d->element[2] = el_circuit_add_impedance(c, t + SUPPLY_PE, star, half);
        }
        break;
      }
      case EL_BREAKER:
      case EL_FUSE:
      case EL_SWITCH:
        if (conducting(d)) {
          d->element[0] = el_circuit_add_impedance(c, t + TWO_IN, t + TWO_OUT, d->contact_ohm);
        }
        break;
      case EL_RCD:
        if (conducting(d)) {
          d->element[0] = el_circuit_add_impedance(c, t + RCD_L_IN, t + RCD_L_OUT, d->contact_ohm);
          d->element[1] = el_circuit_add_impedance(c, t + RCD_N_IN, t + RCD_N_OUT, d->contact_ohm);
          if (d->test_pressed && d->residual_rated_a > 0.0) {
            /* The test circuit bypasses the N pole, so its current is residual current. */
            const double ohm = 230.0 / (RCD_TEST_MULTIPLE * d->residual_rated_a);
            el_circuit_add_impedance(c, t + RCD_L_OUT, t + RCD_N_IN, ohm);
          }
        }
        break;
      case EL_LOAD:
        if (d->plugged_into >= 0) {
          const int s = model->devices[d->plugged_into].first_node;
          for (int k = 0; k < 3; k++) {
            el_circuit_add_impedance(c, t + k, s + k, PLUG_CONTACT_OHM);
          }
        }
        if (d->on) {
          d->element[0] = el_circuit_add_impedance(c, t + LOAD_L, t + LOAD_N, load_ohm(d));
        }
        break;
      case EL_SOCKET:
      case EL_DEVICE_TYPES:
        break;
    }
  }

  if (inject_a >= 0 && inject_b >= 0) {
    el_circuit_add_current_source(c, inject_b, inject_a, 1.0);
  }
  return el_circuit_solve(c, EL_GMIN);
}

static void read_results(ElModel* model) {
  const ElCircuit* c = &model->circuit;
  for (size_t i = 0; i < model->connection_count; i++) {
    ElConnection* k = &model->connections[i];
    k->current_a = k->element >= 0 ? cabs(el_circuit_current(c, k->element)) : 0.0;
  }
  for (size_t i = 0; i < model->fault_count; i++) {
    model->faults[i].current_a = cabs(el_circuit_current(c, model->faults[i].element));
  }
  for (size_t i = 0; i < model->device_count; i++) {
    ElDevice* d = &model->devices[i];
    d->current_a = d->element[0] >= 0 ? cabs(el_circuit_current(c, d->element[0])) : 0.0;
    d->residual_a = 0.0;
    d->power_w = 0.0;
    d->voltage_v_measured = 0.0;
    if (d->type == EL_RCD && d->element[0] >= 0) {
      d->residual_a = cabs(el_circuit_current(c, d->element[0]) + el_circuit_current(c, d->element[1]));
    } else if (d->type == EL_LOAD) {
      const ElPhasor u = el_circuit_voltage(c, d->first_node + LOAD_L) - el_circuit_voltage(c, d->first_node + LOAD_N);
      d->voltage_v_measured = cabs(u);
      if (d->element[0] >= 0) {
        const ElPhasor current = el_circuit_current(c, d->element[0]);
        d->power_w = creal(u * conj(current));
      }
    }
  }
}

/* Thermal state: once per step with the currents before any trip of this step. */
static void update_heat(ElModel* model, double dt) {
  for (size_t i = 0; i < model->device_count; i++) {
    ElDevice* d = &model->devices[i];
    if (d->rated_current_a <= 0.0) {
      continue;
    }
    if (d->type == EL_BREAKER) {
      const double ratio = d->current_a / d->rated_current_a;
      d->heat = ratio > 1.0 ? d->heat + (ratio * ratio - 1.0) * dt : fmax(0.0, d->heat - BREAKER_COOLING * dt);
    } else if (d->type == EL_FUSE) {
      const double excess = d->current_a * d->current_a - d->rated_current_a * d->rated_current_a;
      d->heat = excess > 0.0 ? d->heat + excess * dt
                             : fmax(0.0, d->heat - 0.1 * d->rated_current_a * d->rated_current_a * dt);
    }
  }
}

/* Checks all protective devices; returns true if one tripped. */
static bool check_protection(ElModel* model) {
  bool any = false;
  for (size_t i = 0; i < model->device_count; i++) {
    ElDevice* d = &model->devices[i];
    if (!conducting(d)) {
      continue;
    }
    if (d->type == EL_BREAKER) {
      const double ratio = d->rated_current_a > 0.0 ? d->current_a / d->rated_current_a : 0.0;
      if (ratio >= d->magnetic_multiple) {
        d->tripped = true;
        push_event(model, "%s tripped (short circuit, %.0f A = %.1f x In)", d->id, d->current_a, ratio);
      } else if (d->heat >= d->thermal_limit) {
        d->tripped = true;
        push_event(model, "%s tripped (overload, %.1f A = %.2f x In)", d->id, d->current_a, ratio);
      }
    } else if (d->type == EL_FUSE) {
      if (d->heat >= d->thermal_limit) {
        d->tripped = true;
        push_event(model, "%s blown (%.1f A)", d->id, d->current_a);
      }
    } else if (d->type == EL_RCD) {
      if (d->residual_a >= RCD_TRIP_FRACTION * d->residual_rated_a) {
        d->tripped = true;
        push_event(model, "%s tripped (residual current %.1f mA%s)", d->id, d->residual_a * 1000.0,
                   d->test_pressed ? ", test button" : "");
      }
    }
    any = any || d->tripped;
  }
  return any;
}

bool el_model_step(ElModel* model, double dt_s) {
  bool ok = true;
  for (int iteration = 0; iteration < MAX_TRIP_ITERATIONS; iteration++) {
    if (!build_and_solve(model, false, -1, -1)) {
      ok = false;
      break;
    }
    read_results(model);
    if (iteration == 0) {
      update_heat(model, dt_s);
    }
    if (!check_protection(model)) {
      break;
    }
  }
  for (size_t i = 0; i < model->device_count; i++) {
    model->devices[i].test_pressed = false;
  }
  model->time_s += dt_s;
  return ok;
}

/* ------------------------------------------------------------------------- */
/* Measurements                                                               */
/* ------------------------------------------------------------------------- */

bool el_model_measure_voltage(ElModel* model, const char* a, const char* b, ElPhasor* voltage) {
  const int na = el_model_terminal_node(model, a);
  const int nb = el_model_terminal_node(model, b);
  if (na < 0 || nb < 0 || !build_and_solve(model, false, -1, -1)) {
    return false;
  }
  *voltage = el_circuit_voltage(&model->circuit, na) - el_circuit_voltage(&model->circuit, nb);
  read_results(model);
  return true;
}

bool el_model_measure_impedance(ElModel* model, const char* a, const char* b, ElPhasor* impedance) {
  const int na = el_model_terminal_node(model, a);
  const int nb = el_model_terminal_node(model, b);
  if (na < 0 || nb < 0 || na == nb) {
    return false;
  }
  /* Linear network: the Thevenin impedance does not depend on the sources. */
  const bool ok = build_and_solve(model, true, na, nb);
  if (ok) {
    *impedance = el_circuit_voltage(&model->circuit, na) - el_circuit_voltage(&model->circuit, nb);
  }
  /* Restore the normal solution for later readings. */
  build_and_solve(model, false, -1, -1);
  read_results(model);
  return ok;
}

bool el_model_measure_insulation(ElModel* model, const char* a, const char* b, double* ohm) {
  ElPhasor voltage;
  if (!el_model_measure_voltage(model, a, b, &voltage) || cabs(voltage) > EL_SAFE_VOLTAGE) {
    return false;
  }
  ElPhasor impedance;
  if (!el_model_measure_impedance(model, a, b, &impedance)) {
    return false;
  }
  *ohm = cabs(impedance);
  return true;
}
