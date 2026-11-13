#ifndef IOTWIN_LIGHT_ECU_DIAG_H
#define IOTWIN_LIGHT_ECU_DIAG_H

/**
 * @file light_ecu_diag.h
 * @brief Output stage, electrical model and UDS diagnostics of the simulated light ECU
 *        (see docs/dev/diagnostics.md and docs/dev/power.md).
 *
 * The lamp output is a PowerPath: 12.6 V supply -> 3 A slow fuse -> smart high-side
 * switch with PWM (brightness) and current sense -> 21 W bulb. Injected faults change
 * that circuit, monitors turn the measured values into DTCs the way a real ECU would:
 *  - short to ground: the smart switch shuts down (restart every 100 ms)   B1001-11
 *  - open load: no current while the output is on                          B1001-13
 *  - overload (three times the power): current above 1.5 x nominal          B1001-19,
 *    the fuse blows after about two seconds                                 B1003-13
 *  - supply voltage below 10.5 V                                            B1002-16
 *
 * UDS: requests on 0x7E0 (and functional 0x7DF), responses on 0x7E8.
 * LAMP_STATUS (0x101) reports current, supply voltage, duty and flags every 200 ms.
 *
 * Free of I/O and threads, unit tested on the host.
 */

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"
#include "light_ecu.h"
#include "power_path.h"
#include "uds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_ECU_DIAG_REQUEST_ID 0x7E0u
#define LIGHT_ECU_DIAG_RESPONSE_ID 0x7E8u
#define LIGHT_ECU_ID_LAMP_STATUS 0x101u
#define LIGHT_ECU_STATUS_PERIOD_MS 200u

/** @name DTCs */
/**@{*/
#define LIGHT_ECU_DTC_LAMP_SHORT 0x900111u   /**< B1001-11 lamp output short to ground */
#define LIGHT_ECU_DTC_LAMP_OPEN 0x900113u    /**< B1001-13 lamp output open circuit */
#define LIGHT_ECU_DTC_OVERCURRENT 0x900119u  /**< B1001-19 lamp current above threshold */
#define LIGHT_ECU_DTC_SUPPLY_LOW 0x900216u   /**< B1002-16 supply voltage below threshold */
#define LIGHT_ECU_DTC_FUSE_BLOWN 0x900313u   /**< B1003-13 lamp fuse blown (circuit open) */
#define LIGHT_ECU_DTC_COUNT 5u
/**@}*/

/** @name Data identifiers (besides F186, F187, F189, F18C, F190) */
/**@{*/
#define LIGHT_ECU_DID_LAMP_OUTPUT 0x0100u    /**< 1 byte, output state (read) */
#define LIGHT_ECU_DID_SWITCH_INPUT 0x0101u   /**< 1 byte, last SWITCH_STATE (read) */
#define LIGHT_ECU_DID_CYCLE_TIME 0x0102u     /**< uint16 ms, LAMP_CMD cycle, 0 = off (read/write) */
#define LIGHT_ECU_DID_BLINK_PERIOD 0x0103u   /**< uint16 ms, 0 = not blinking (read/write) */
#define LIGHT_ECU_DID_SUPPLY_VOLTAGE 0x0104u /**< uint16 mV, measured supply (read) */
#define LIGHT_ECU_DID_LEVEL 0x0105u          /**< 1 byte, brightness 0..100 % (read/write) */
#define LIGHT_ECU_DID_CURRENT 0x0106u        /**< uint16 mA, lamp current (read) */
#define LIGHT_ECU_DID_FUSE 0x0107u           /**< blown (1 byte), heat in % of melting (1 byte) */
/**@}*/

#define LIGHT_ECU_SUPPLY_NORMAL_MV 12600u
#define LIGHT_ECU_SUPPLY_LOW_MV 9800u
#define LIGHT_ECU_SUPPLY_THRESHOLD_V 10.5
#define LIGHT_ECU_OVERCURRENT_FACTOR 1.5
#define LIGHT_ECU_OVERCURRENT_MS 200u
#define LIGHT_ECU_OVERLOAD_FACTOR 3.0

/** @name LAMP_STATUS flags (data[5]) */
/**@{*/
#define LIGHT_ECU_STATUS_FUSE_BLOWN 0x01u
#define LIGHT_ECU_STATUS_OVERCURRENT 0x02u
#define LIGHT_ECU_STATUS_SHORT_SHUTDOWN 0x04u
/**@}*/

/** @brief Injectable faults (bit mask). */
typedef enum {
  LIGHT_FAULT_LAMP_SHORT = 0x01,
  LIGHT_FAULT_LAMP_OPEN = 0x02,
  LIGHT_FAULT_SUPPLY_LOW = 0x04,
  LIGHT_FAULT_OVERLOAD = 0x08
} LightEcuFault;

typedef struct {
  LightEcu* ecu;
  UdsServer uds;
  UdsDtc dtcs[LIGHT_ECU_DTC_COUNT];
  PowerPath power;
  PowerReading reading;     /**< Last result of the electrical model. */
  uint8_t faults;           /**< Active LightEcuFault bits. */
  uint32_t resets;          /**< ECUReset requests executed. */
  uint32_t overcurrent_ms;  /**< Time the current has been above the threshold. */
  uint64_t last_step_ms;
  uint64_t last_status_ms;
  bool stepped;
  const char* software_version;
} LightEcuDiag;

/**
 * @brief Initialises diagnostics and the output stage for @p ecu.
 * @param send CAN transmit function for the UDS responses.
 * @param software_version Reported in DID F189.
 */
void light_ecu_diag_init(LightEcuDiag* diag, LightEcu* ecu, bool (*send)(void* context, const CanFrame* frame),
                         void* send_context, const char* software_version);

/** @brief Hands a received frame to the UDS server. @return true if it was a diagnostic request. */
bool light_ecu_diag_on_frame(LightEcuDiag* diag, const CanFrame* frame, uint64_t now_ms);

/**
 * @brief Runs the electrical model, the fault monitors and the UDS timers.
 * Call every control step before light_ecu_step() (it sets the output inhibit).
 */
void light_ecu_diag_step(LightEcuDiag* diag, uint64_t now_ms);

/** @brief LAMP_STATUS every LIGHT_ECU_STATUS_PERIOD_MS. @return true if @p out has to be sent. */
bool light_ecu_diag_status(LightEcuDiag* diag, uint64_t now_ms, CanFrame* out);

/** @brief Activates or removes an injected fault. */
void light_ecu_diag_set_fault(LightEcuDiag* diag, LightEcuFault fault, bool active);

/** @brief Puts in a new fuse. */
void light_ecu_diag_replace_fuse(LightEcuDiag* diag);

/** @brief Parses "short", "open", "supply" or "overload". */
bool light_ecu_fault_from_name(const char* name, LightEcuFault* fault);

const char* light_ecu_fault_name(LightEcuFault fault);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_LIGHT_ECU_DIAG_H */
