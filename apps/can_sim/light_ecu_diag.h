#ifndef IOTWIN_LIGHT_ECU_DIAG_H
#define IOTWIN_LIGHT_ECU_DIAG_H

/**
 * @file light_ecu_diag.h
 * @brief UDS diagnostics of the simulated light ECU (see docs/dev/diagnostics.md).
 *
 * Addressing: requests on 0x7E0 (and functional 0x7DF), responses on 0x7E8.
 *
 * Faults can be injected (console command "fault"); monitors turn them into
 * DTCs the way a real ECU would:
 *  - lamp output short to ground (B1001-11): the ECU switches the output off,
 *  - lamp output open circuit (B1001-13): only detectable while the lamp is on,
 *  - supply voltage low (B1002-16).
 *
 * Free of I/O and threads, unit tested on the host.
 */

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"
#include "light_ecu.h"
#include "uds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_ECU_DIAG_REQUEST_ID 0x7E0u
#define LIGHT_ECU_DIAG_RESPONSE_ID 0x7E8u

/** @name DTCs */
/**@{*/
#define LIGHT_ECU_DTC_LAMP_SHORT 0x900111u /**< B1001-11 lamp output short to ground */
#define LIGHT_ECU_DTC_LAMP_OPEN 0x900113u  /**< B1001-13 lamp output open circuit */
#define LIGHT_ECU_DTC_SUPPLY_LOW 0x900216u /**< B1002-16 supply voltage below threshold */
#define LIGHT_ECU_DTC_COUNT 3u
/**@}*/

/** @name Data identifiers (besides F186, F187, F189, F18C, F190) */
/**@{*/
#define LIGHT_ECU_DID_LAMP_OUTPUT 0x0100u    /**< 1 byte, output state (read) */
#define LIGHT_ECU_DID_SWITCH_INPUT 0x0101u   /**< 1 byte, last SWITCH_STATE (read) */
#define LIGHT_ECU_DID_CYCLE_TIME 0x0102u     /**< uint16 ms, LAMP_CMD cycle, 0 = off (read/write) */
#define LIGHT_ECU_DID_BLINK_PERIOD 0x0103u   /**< uint16 ms, 0 = not blinking (read/write) */
#define LIGHT_ECU_DID_SUPPLY_VOLTAGE 0x0104u /**< uint16 mV, simulated supply (read) */
/**@}*/

#define LIGHT_ECU_SUPPLY_NORMAL_MV 12600u
#define LIGHT_ECU_SUPPLY_LOW_MV 9800u

/** @brief Injectable faults (bit mask). */
typedef enum {
  LIGHT_FAULT_LAMP_SHORT = 0x01,
  LIGHT_FAULT_LAMP_OPEN = 0x02,
  LIGHT_FAULT_SUPPLY_LOW = 0x04
} LightEcuFault;

typedef struct {
  LightEcu* ecu;
  UdsServer uds;
  UdsDtc dtcs[LIGHT_ECU_DTC_COUNT];
  uint8_t faults;           /**< Active LightEcuFault bits. */
  uint32_t resets;          /**< ECUReset requests executed. */
  const char* software_version;
} LightEcuDiag;

/**
 * @brief Initialises diagnostics for @p ecu.
 * @param send CAN transmit function for the responses.
 * @param software_version Reported in DID F189.
 */
void light_ecu_diag_init(LightEcuDiag* diag, LightEcu* ecu, bool (*send)(void* context, const CanFrame* frame),
                         void* send_context, const char* software_version);

/** @brief Hands a received frame to the UDS server. @return true if it was a diagnostic request. */
bool light_ecu_diag_on_frame(LightEcuDiag* diag, const CanFrame* frame, uint64_t now_ms);

/** @brief Runs the fault monitors and the UDS timers; call every control step before light_ecu_step(). */
void light_ecu_diag_step(LightEcuDiag* diag, uint64_t now_ms);

/** @brief Activates or removes an injected fault. */
void light_ecu_diag_set_fault(LightEcuDiag* diag, LightEcuFault fault, bool active);

/** @brief Parses "short", "open" or "supply". */
bool light_ecu_fault_from_name(const char* name, LightEcuFault* fault);

const char* light_ecu_fault_name(LightEcuFault fault);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_LIGHT_ECU_DIAG_H */
