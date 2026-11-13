#include "light_ecu_diag.h"

#include <string.h>

#define SPARE_PART_NUMBER "IOTWIN-LIGHT-ECU"
#define SERIAL_NUMBER "LECU-SIM-0001"
#define VIN "IOTWIN0LIGHTDEMO1"
#define MAX_PERIOD_MS 60000u
#define MIN_PERIOD_MS 50u

static int put_text(const char* text, uint8_t* out, size_t capacity) {
  const size_t length = strlen(text);
  if (length > capacity) {
    return -(int)UDS_NRC_RESPONSE_TOO_LONG;
  }
  memcpy(out, text, length);
  return (int)length;
}

static int put_u16(uint32_t value, uint8_t* out, size_t capacity) {
  if (capacity < 2) {
    return -(int)UDS_NRC_RESPONSE_TOO_LONG;
  }
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
  return 2;
}

static int read_did(void* context, uint16_t did, uint8_t* out, size_t capacity) {
  LightEcuDiag* diag = (LightEcuDiag*)context;
  if (capacity < 1) {
    return -(int)UDS_NRC_RESPONSE_TOO_LONG;
  }
  switch (did) {
    case UDS_DID_SPARE_PART_NUMBER:
      return put_text(SPARE_PART_NUMBER, out, capacity);
    case UDS_DID_SOFTWARE_VERSION:
      return put_text(diag->software_version, out, capacity);
    case UDS_DID_SERIAL_NUMBER:
      return put_text(SERIAL_NUMBER, out, capacity);
    case UDS_DID_VIN:
      return put_text(VIN, out, capacity);
    case LIGHT_ECU_DID_LAMP_OUTPUT:
      out[0] = light_ecu_output(diag->ecu) ? 1u : 0u;
      return 1;
    case LIGHT_ECU_DID_SWITCH_INPUT:
      out[0] = diag->ecu->switch_on ? 1u : 0u;
      return 1;
    case LIGHT_ECU_DID_CYCLE_TIME:
      return put_u16(diag->ecu->cycle_ms, out, capacity);
    case LIGHT_ECU_DID_BLINK_PERIOD:
      return put_u16(diag->ecu->blink_period_ms, out, capacity);
    case LIGHT_ECU_DID_SUPPLY_VOLTAGE:
      return put_u16((diag->faults & LIGHT_FAULT_SUPPLY_LOW) ? LIGHT_ECU_SUPPLY_LOW_MV : LIGHT_ECU_SUPPLY_NORMAL_MV,
                     out, capacity);
    default:
      return -(int)UDS_NRC_REQUEST_OUT_OF_RANGE;
  }
}

static uint8_t write_did(void* context, uint16_t did, const uint8_t* data, size_t length) {
  LightEcuDiag* diag = (LightEcuDiag*)context;
  if (did != LIGHT_ECU_DID_CYCLE_TIME && did != LIGHT_ECU_DID_BLINK_PERIOD) {
    return UDS_NRC_REQUEST_OUT_OF_RANGE;
  }
  if (length != 2) {
    return UDS_NRC_INCORRECT_LENGTH;
  }
  const uint32_t value = ((uint32_t)data[0] << 8) | data[1];
  if (value != 0 && (value < MIN_PERIOD_MS || value > MAX_PERIOD_MS)) {
    return UDS_NRC_REQUEST_OUT_OF_RANGE;
  }
  if (did == LIGHT_ECU_DID_CYCLE_TIME) {
    diag->ecu->cycle_ms = value;
  } else {
    light_ecu_set_blink(diag->ecu, value);
  }
  return 0;
}

static void reset(void* context, uint8_t type) {
  (void)type; /* hard and soft reset restart the application the same way */
  LightEcuDiag* diag = (LightEcuDiag*)context;
  const uint32_t cycle_ms = diag->ecu->cycle_ms;
  light_ecu_init(diag->ecu, cycle_ms);
  for (size_t i = 0; i < LIGHT_ECU_DTC_COUNT; i++) {
    uds_dtc_start_operation_cycle(&diag->dtcs[i]); /* DTC memory survives the reset */
  }
  diag->resets++;
}

void light_ecu_diag_init(LightEcuDiag* diag, LightEcu* ecu, bool (*send)(void* context, const CanFrame* frame),
                         void* send_context, const char* software_version) {
  memset(diag, 0, sizeof(*diag));
  diag->ecu = ecu;
  diag->software_version = software_version != NULL ? software_version : "0.0.0";
  uds_dtc_init(&diag->dtcs[0], LIGHT_ECU_DTC_LAMP_SHORT);
  uds_dtc_init(&diag->dtcs[1], LIGHT_ECU_DTC_LAMP_OPEN);
  uds_dtc_init(&diag->dtcs[2], LIGHT_ECU_DTC_SUPPLY_LOW);

  UdsServerConfig config;
  uds_server_default_config(&config, LIGHT_ECU_DIAG_REQUEST_ID, LIGHT_ECU_DIAG_RESPONSE_ID);
  config.send = send;
  config.send_context = send_context;
  config.handlers.read_did = read_did;
  config.handlers.write_did = write_did;
  config.handlers.reset = reset;
  config.handlers.context = diag;
  config.dtcs = diag->dtcs;
  config.dtc_count = LIGHT_ECU_DTC_COUNT;
  uds_server_init(&diag->uds, &config);
}

bool light_ecu_diag_on_frame(LightEcuDiag* diag, const CanFrame* frame, uint64_t now_ms) {
  return uds_server_on_frame(&diag->uds, frame, now_ms * 1000u);
}

void light_ecu_diag_step(LightEcuDiag* diag, uint64_t now_ms) {
  const bool shorted = (diag->faults & LIGHT_FAULT_LAMP_SHORT) != 0;
  /* Short circuit protection: the output driver switches off. */
  light_ecu_set_output_inhibit(diag->ecu, shorted);
  uds_dtc_report(&diag->dtcs[0], shorted);
  /* An open load can only be detected while the output is on. */
  if (light_ecu_output(diag->ecu)) {
    uds_dtc_report(&diag->dtcs[1], (diag->faults & LIGHT_FAULT_LAMP_OPEN) != 0);
  }
  uds_dtc_report(&diag->dtcs[2], (diag->faults & LIGHT_FAULT_SUPPLY_LOW) != 0);

  uds_server_poll(&diag->uds, now_ms * 1000u);
}

void light_ecu_diag_set_fault(LightEcuDiag* diag, LightEcuFault fault, bool active) {
  if (active) {
    diag->faults = (uint8_t)(diag->faults | fault);
  } else {
    diag->faults = (uint8_t)(diag->faults & ~fault);
  }
}

bool light_ecu_fault_from_name(const char* name, LightEcuFault* fault) {
  if (strcmp(name, "short") == 0) {
    *fault = LIGHT_FAULT_LAMP_SHORT;
  } else if (strcmp(name, "open") == 0) {
    *fault = LIGHT_FAULT_LAMP_OPEN;
  } else if (strcmp(name, "supply") == 0) {
    *fault = LIGHT_FAULT_SUPPLY_LOW;
  } else {
    return false;
  }
  return true;
}

const char* light_ecu_fault_name(LightEcuFault fault) {
  switch (fault) {
    case LIGHT_FAULT_LAMP_SHORT: return "lamp output short to ground";
    case LIGHT_FAULT_LAMP_OPEN: return "lamp output open circuit";
    case LIGHT_FAULT_SUPPLY_LOW: return "supply voltage low";
    default: return "unknown fault";
  }
}
