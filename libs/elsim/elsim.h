#ifndef IOTWIN_ELSIM_H
#define IOTWIN_ELSIM_H

/**
 * @file elsim.h
 * @brief Electrical installation of a house: model, time steps, protection, measurements.
 *
 * See docs/adr/0007-electrical-simulation-core.md and docs/dev/electrical.md.
 *
 * - A **device** has typed **terminals**, referred to as "device:terminal"
 *   (e.g. "lamp_living:L"). Every terminal is a node of the network.
 * - A **connection** is one conductor between two terminals (role L, N or
 *   PE); its resistance comes from its **cable** (copper, length,
 *   cross-section) or is a short internal link.
 * - el_model_step() solves the network (complex phasors, 50 Hz), evaluates
 *   breakers, fuses and RCDs, re-solves after a trip and records events.
 * - Measurements see the same network: voltage and Thevenin impedance
 *   between two terminals (loop impedance, continuity, insulation).
 *
 * Terminals per type:
 *   supply  L1 L2 L3 N PE       breaker, fuse, switch, dimmer  in out
 *   rcd     L_in N_in L_out N_out
 *   load    L N PE              socket, junction  L N PE
 *
 * Change the state only through the el_model_* functions: they mark the
 * network as changed (ElModel.revision), which dimmers need.
 * For running the model in its own (real-time) thread see el_session.h.
 */

#include <stdbool.h>
#include <stddef.h>

#include "el_circuit.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EL_ID_MAX 48
#define EL_TEXT_MAX 112
#define EL_MAX_EVENTS 64
#define EL_MAX_TERMINALS 5

/** Copper at 20 °C in Ohm mm^2 / m. */
#define EL_COPPER_RESISTIVITY 0.0178
/** Resistance of a short internal link (busbar, jumper) without a cable. */
#define EL_LINK_OHM 0.001
/** A voltage below this counts as "no voltage" for safety checks (V). */
#define EL_SAFE_VOLTAGE 50.0

typedef enum {
  EL_SUPPLY,
  EL_BREAKER,
  EL_FUSE,
  EL_RCD,
  EL_SWITCH,
  EL_LOAD,
  EL_SOCKET,
  EL_JUNCTION, /**< junction box / terminal block: L N PE, no internal elements */
  EL_DIMMER,   /**< series dimmer in/out with a level 0..1 */
  EL_DEVICE_TYPES
} ElDeviceType;

typedef enum { EL_TN_S, EL_TN_C_S, EL_TT } ElEarthing;

typedef struct {
  char id[EL_ID_MAX];
  ElDeviceType type;
  char component[EL_ID_MAX];
  char label[EL_TEXT_MAX];
  char room[EL_ID_MAX];
  int first_node;    /**< Node of terminal 0; terminal i is first_node + i. */
  int internal_node; /**< First internal node (supply only). */

  /* parameters */
  double voltage_v;           /**< supply: phase voltage; load: rated voltage */
  double frequency_hz;        /**< supply */
  double loop_impedance_ohm;  /**< supply: external loop impedance Ze (L-N and L-PE) */
  ElEarthing earthing;        /**< supply */
  double earth_electrode_ohm; /**< supply, TT: earth electrode of the house (R_A) */
  double source_earth_ohm;    /**< supply: earthing of the transformer star point (R_B) */
  double rated_current_a;     /**< breaker, fuse, rcd */
  double magnetic_multiple;   /**< breaker: instantaneous trip at this multiple of In */
  double thermal_limit;       /**< breaker: integral of ((I/In)^2 - 1) dt in s; fuse: melting I^2 t in A^2 s */
  double residual_rated_a;    /**< rcd: rated residual current (I delta n) */
  double rated_power_w;       /**< load */
  double contact_ohm;         /**< breaker, fuse, rcd pole, switch/dimmer contact */
  int plugged_into;           /**< load: index of the socket it is plugged into, -1 = fixed wiring */

  /* state */
  bool on;           /**< handle of switch/breaker/rcd, a load's own switch */
  bool tripped;      /**< breaker/rcd tripped, fuse blown */
  double heat;       /**< thermal state of breaker/fuse */
  bool test_pressed; /**< rcd test button, for the next step */
  double level;      /**< dimmer: 0..1 (share of the full power of its load) */
  double reactance_ohm; /**< dimmer: series reactance that sets the level (computed) */

  /* results of the last step */
  double current_a;  /**< load, breaker, fuse, switch; rcd: line pole; supply: L1 */
  double residual_a; /**< rcd */
  double voltage_v_measured; /**< load, socket, junction: across L-N */
  double power_w;    /**< load */
  int element[3];    /**< circuit elements of the last solution, -1 if not present */
} ElDevice;

typedef struct {
  char id[EL_ID_MAX];
  char type[EL_ID_MAX];
  double cross_section_mm2;
  double length_m;
} ElCable;

typedef struct {
  char id[EL_ID_MAX];
  char role[8];      /**< "L", "N", "PE" or "" */
  int a;
  int b;
  int cable;         /**< index or -1 */
  double resistance_ohm;
  bool open;         /**< interrupted conductor (fault) */
  double current_a;
  int element;
} ElConnection;

typedef struct {
  char description[EL_TEXT_MAX];
  int a;
  int b;
  double resistance_ohm;
  double current_a;
  int element;
} ElFault;

typedef struct {
  double time_s;
  char text[EL_TEXT_MAX + EL_ID_MAX];
} ElEvent;

typedef struct {
  char id[EL_ID_MAX];
  char name[EL_TEXT_MAX];
  ElDevice* devices;
  size_t device_count;
  ElCable* cables;
  size_t cable_count;
  ElConnection* connections;
  size_t connection_count;
  ElFault* faults;
  size_t fault_count;
  int node_count; /**< incl. node 0 (earth) */
  double time_s;
  ElCircuit circuit;
  ElEvent events[EL_MAX_EVENTS];
  size_t event_head;
  size_t event_count;
  unsigned revision;        /**< incremented by every change of the network */
  unsigned dimmer_revision; /**< revision the dimmer reactances were computed for */
} ElModel;

void el_model_init(ElModel* model);
void el_model_free(ElModel* model);

/** @name Building an installation */
/**@{*/
/** @return Device index, -1 if the id exists or memory runs out. Parameters get type defaults. */
int el_model_add_device(ElModel* model, const char* id, ElDeviceType type);
int el_model_add_cable(ElModel* model, const char* id, const char* type, double cross_section_mm2, double length_m);
/** @return Connection index, -1 on unknown terminals. @p cable may be NULL. */
int el_model_connect(ElModel* model, const char* id, const char* from, const char* to, const char* role,
                     const char* cable);
/** @brief Plugs a load into a socket (NULL socket = unplug). */
bool el_model_plug(ElModel* model, const char* load, const char* socket);
/**@}*/

/** @name Lookup */
/**@{*/
int el_model_find_device(const ElModel* model, const char* id);
int el_model_find_connection(const ElModel* model, const char* id);
/** @brief Node of "device:terminal", -1 if unknown. */
int el_model_terminal_node(const ElModel* model, const char* reference);
const char* el_device_type_name(ElDeviceType type);
bool el_device_type_from_name(const char* name, ElDeviceType* type);
int el_terminal_count(ElDeviceType type);
const char* el_terminal_name(ElDeviceType type, int terminal);
/** @brief Load is supplied: switched on and at least 1 % of its rated power (dimmed loads count). */
bool el_device_powered(const ElDevice* device);
/**@}*/

/** @name Operating the installation (takes effect in the next step) */
/**@{*/
/** @brief Switch, breaker or RCD handle, a load's own switch. */
bool el_model_set_on(ElModel* model, const char* id, bool on);
/** @brief Re-arms a tripped breaker/RCD (handle on) or replaces a blown fuse. */
bool el_model_reset(ElModel* model, const char* id);
/** @brief Presses the test button of an RCD for one step. */
bool el_model_press_test(ElModel* model, const char* id);
/** @brief Level of a dimmer, 0..1 (clamped). */
bool el_model_set_level(ElModel* model, const char* id, double level);
/** @brief Marks the network as changed after writing device fields directly. */
void el_model_touch(ElModel* model);
/** @return Fault index: an impedance between two terminals (short circuit, insulation or earth fault). */
int el_model_add_fault(ElModel* model, const char* a, const char* b, double resistance_ohm, const char* description);
/** @brief Interrupts (or restores) a conductor. */
bool el_model_set_connection_open(ElModel* model, const char* id, bool open);
void el_model_clear_faults(ElModel* model);
/**@}*/

/**
 * @brief Advances the time by @p dt_s: solve, protection, results, events.
 * @return false if the network cannot be solved.
 */
bool el_model_step(ElModel* model, double dt_s);

/** @brief Oldest unread event, false if there is none. */
bool el_model_next_event(ElModel* model, ElEvent* event);

/** @name Measurements (no time passes) */
/**@{*/
/** @brief Voltage V(a) - V(b) of the present state. */
bool el_model_measure_voltage(ElModel* model, const char* a, const char* b, ElPhasor* voltage);
/** @brief Impedance seen between a and b (loop impedance, continuity), sources as in the network. */
bool el_model_measure_impedance(ElModel* model, const char* a, const char* b, ElPhasor* impedance);
/**
 * @brief Insulation resistance between a and b. Like a real tester it refuses
 *        (returns false) while a voltage above EL_SAFE_VOLTAGE is present.
 */
bool el_model_measure_insulation(ElModel* model, const char* a, const char* b, double* ohm);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_ELSIM_H */
