#ifndef IOTWIN_LIGHT_ECU_H
#define IOTWIN_LIGHT_ECU_H

/**
 * @file light_ecu.h
 * @brief Simulated light control unit (the demo ECU of can_sim).
 *
 * Signals (see docs/dev/protocol.md):
 *  - 0x200 SWITCH_STATE (input):  data[0] bit 0 = position of the wall switch
 *  - 0x201 DIMMER_CMD   (input):  data[0] = brightness 0..100 %
 *  - 0x100 LAMP_CMD     (output): data[0] bit 0 = lamp on, data[1] = brightness 0..100 %
 *
 * The ECU sets the lamp to the switch position, can blink the lamp on its
 * own, and sends LAMP_CMD on every change plus cyclically, so a node that
 * joins the bus later learns the current state within one cycle.
 *
 * The logic is free of I/O and time sources so it can be unit tested.
 */

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_ECU_ID_LAMP_CMD 0x100u
#define LIGHT_ECU_ID_SWITCH_STATE 0x200u
#define LIGHT_ECU_ID_DIMMER_CMD 0x201u
#define LIGHT_ECU_DEFAULT_CYCLE_MS 500u
#define LIGHT_ECU_DEFAULT_LEVEL 100u

/** @brief ECU state. */
typedef struct {
  bool lamp_on;             /**< Commanded lamp state. */
  bool switch_on;           /**< Last position reported by SWITCH_STATE. */
  bool output_inhibit;      /**< Output switched off by a protection (e.g. short circuit). */
  uint8_t level;            /**< Brightness 0..100 % (PWM duty of the output). */
  uint32_t cycle_ms;        /**< Period of the cyclic LAMP_CMD message (0 = only on change). */
  uint32_t blink_period_ms; /**< Toggle period while blinking, 0 = not blinking. */
  uint64_t last_send_ms;    /**< Time of the last LAMP_CMD. */
  uint64_t last_toggle_ms;  /**< Time of the last blink toggle. */
  bool dirty;               /**< State changed, send immediately. */
  bool started;             /**< First step executed. */
} LightEcu;

/** @brief Initialises the ECU (lamp off, not blinking). */
void light_ecu_init(LightEcu* ecu, uint32_t cycle_ms);

/**
 * @brief Processes a received frame.
 * @return true if the frame was a valid SWITCH_STATE or DIMMER_CMD frame.
 */
bool light_ecu_on_frame(LightEcu* ecu, const CanFrame* frame);

/** @brief Sets the lamp (stops blinking). */
void light_ecu_set_lamp(LightEcu* ecu, bool on);

/** @brief Sets the brightness (clamped to 0..100 %). */
void light_ecu_set_level(LightEcu* ecu, uint32_t percent);

/** @brief Starts blinking with the given toggle period; 0 stops blinking. */
void light_ecu_set_blink(LightEcu* ecu, uint32_t period_ms);

/** @brief Switches the output off regardless of the command (protection), or releases it. */
void light_ecu_set_output_inhibit(LightEcu* ecu, bool inhibit);

/** @brief State of the lamp output: commanded and not inhibited. */
bool light_ecu_output(const LightEcu* ecu);

/** @brief Forces LAMP_CMD to be sent in the next step (e.g. after reconnecting). */
void light_ecu_request_send(LightEcu* ecu);

/**
 * @brief Runs one control step.
 * @param now_ms Current monotonic time in milliseconds.
 * @param out Receives the LAMP_CMD frame if one has to be sent.
 * @param state_changed Optional, set to true if the frame reports a new lamp state.
 * @return true if @p out has to be sent.
 */
bool light_ecu_step(LightEcu* ecu, uint64_t now_ms, CanFrame* out, bool* state_changed);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_LIGHT_ECU_H */
