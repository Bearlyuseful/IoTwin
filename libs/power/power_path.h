#ifndef IOTWIN_POWER_PATH_H
#define IOTWIN_POWER_PATH_H

/**
 * @file power_path.h
 * @brief Electrical model of one switched output of an ECU (quasi-static, per time step).
 *
 *   supply (U0, Ri) -- fuse -- smart high-side switch (PWM) -- wire -- resistive load -- GND
 *
 * - Supply: open circuit voltage and internal resistance; the terminal voltage
 *   drops with the load current.
 * - Fuse: slow blow, I2t model. While the RMS current is above the rating the
 *   excess (I^2 - In^2) * t heats it up; it blows when the melting integral is
 *   reached and cools down otherwise.
 * - Smart switch (e.g. PROFET type): PWM with duty 0..1, current limit with
 *   shutdown, automatic restart after restart_ms (thermal retry), current sense.
 * - Load: resistive, R = Un^2 / Pn; faults: short (R = 0), open (no current),
 *   overload factor (power factor > 1 = lower resistance, e.g. wrong bulb).
 *
 * Currents are averaged over the PWM period, the fuse uses the RMS value.
 * Portable C without I/O, unit tested on the host.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  double open_circuit_v; /**< Source voltage without load. */
  double internal_ohm;   /**< Internal resistance incl. supply wiring. */
} PowerSupply;

typedef struct {
  double rated_a;     /**< Rated current. */
  double melting_i2t; /**< A^2 s above the rating until it blows. */
  double cooling_i2t; /**< A^2 s per second it cools down below the rating. */
  double heat;        /**< Accumulated A^2 s. */
  bool blown;
} Fuse;

typedef struct {
  double current_limit_a; /**< Shutdown threshold. */
  uint32_t restart_ms;    /**< Retry after a shutdown. */
  bool tripped;           /**< Switched off by the protection. */
  uint32_t tripped_ms;    /**< Time since the shutdown. */
} SmartSwitch;

typedef struct {
  double rated_power_w;
  double rated_voltage_v;
  double power_factor; /**< 1 = nominal, 3 = three times the power (overload). */
  bool shorted;
  bool open;
} ResistiveLoad;

typedef struct {
  PowerSupply supply;
  Fuse fuse;
  SmartSwitch output;
  ResistiveLoad load;
  double wire_ohm;
} PowerPath;

/** @brief Result of one step. */
typedef struct {
  double supply_v;   /**< Terminal voltage at the ECU (average). */
  double current_a;  /**< Average load current (current sense). */
  double load_v;     /**< Average voltage across the load. */
  double power_w;    /**< Average power in the load. */
  double duty;       /**< Duty actually applied (0 while off, tripped or blown). */
  bool tripped;      /**< Smart switch is off because of overcurrent. */
  bool fuse_blown;
} PowerReading;

/**
 * @brief Defaults of the light demo: 12.6 V battery with 50 mOhm, 3 A slow fuse
 *        (melting 40 A^2 s), 15 A current limit with 100 ms restart, 20 mOhm wire,
 *        21 W / 12 V bulb.
 */
void power_path_init(PowerPath* path);

/** @brief Nominal on-current of the load at the rated voltage (P / U). */
double power_path_nominal_current(const PowerPath* path);

/**
 * @brief Advances the model by @p dt_ms.
 * @param on Output commanded on.
 * @param duty PWM duty 0..1 (clamped).
 */
void power_path_step(PowerPath* path, bool on, double duty, uint32_t dt_ms, PowerReading* reading);

/** @brief Replaces a blown fuse (cold). */
void power_path_replace_fuse(PowerPath* path);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_POWER_PATH_H */
