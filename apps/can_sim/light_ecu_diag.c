#include "light_ecu_diag.h"

#include <string.h>

#define SPARE_PART_NUMBER "IOTWIN-LIGHT-ECU"
#define SERIAL_NUMBER "LECU-SIM-0001"
#define VIN "IOTWIN0LIGHTDEMO1"
#define MAX_PERIOD_MS 60000u
#define MIN_PERIOD_MS 50u

enum { DTC_SHORT, DTC_OPEN, DTC_OVERCURRENT, DTC_SUPPLY, DTC_FUSE };

static uint32_t round_milli(double value) {
  return value <= 0.0 ? 0u : (uint32_t)(value * 1000.0 + 0.5);
}

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
  if (value > 0xFFFFu) {
    value = 0xFFFFu;
  }
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
  return 2;
}

static int read_did(void* context, uint16_t did, uint8_t* out, size_t capacity) {
  LightEcuDiag* diag = (LightEcuDiag*)context;
  if (capacity < 2) {
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
      return put_u16(round_milli(diag->reading.supply_v), out, capacity);
    case LIGHT_ECU_DID_LEVEL:
      out[0] = diag->ecu->level;
      return 1;
    case LIGHT_ECU_DID_CURRENT:
      return put_u16(round_milli(diag->reading.current_a), out, capacity);
    case LIGHT_ECU_DID_FUSE: {
      const double heat = diag->power.fuse.melting_i2t > 0.0 ? diag->power.fuse.heat / diag->power.fuse.melting_i2t : 0.0;
      out[0] = diag->power.fuse.blown ? 1u : 0u;
      out[1] = (uint8_t)(diag->power.fuse.blown ? 100u : (uint32_t)(heat * 100.0));
      return 2;
    }
    default:
      return -(int)UDS_NRC_REQUEST_OUT_OF_RANGE;
  }
}

static uint8_t write_did(void* context, uint16_t did, const uint8_t* data, size_t length) {
  LightEcuDiag* diag = (LightEcuDiag*)context;
  if (did == LIGHT_ECU_DID_LEVEL) {
    if (length != 1) {
      return UDS_NRC_INCORRECT_LENGTH;
    }
    if (data[0] > 100u) {
      return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    light_ecu_set_level(diag->ecu, data[0]);
    return 0;
  }
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
  diag->overcurrent_ms = 0;
  diag->resets++;
}

void light_ecu_diag_init(LightEcuDiag* diag, LightEcu* ecu, bool (*send)(void* context, const CanFrame* frame),
                         void* send_context, const char* software_version) {
  memset(diag, 0, sizeof(*diag));
  diag->ecu = ecu;
  diag->software_version = software_version != NULL ? software_version : "0.0.0";
  uds_dtc_init(&diag->dtcs[DTC_SHORT], LIGHT_ECU_DTC_LAMP_SHORT);
  uds_dtc_init(&diag->dtcs[DTC_OPEN], LIGHT_ECU_DTC_LAMP_OPEN);
  uds_dtc_init(&diag->dtcs[DTC_OVERCURRENT], LIGHT_ECU_DTC_OVERCURRENT);
  uds_dtc_init(&diag->dtcs[DTC_SUPPLY], LIGHT_ECU_DTC_SUPPLY_LOW);
  uds_dtc_init(&diag->dtcs[DTC_FUSE], LIGHT_ECU_DTC_FUSE_BLOWN);
  power_path_init(&diag->power);
  diag->reading.supply_v = diag->power.supply.open_circuit_v;

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

/* Applies the injected faults to the electrical model. */
static void apply_faults(LightEcuDiag* diag) {
  PowerPath* power = &diag->power;
  power->load.shorted = (diag->faults & LIGHT_FAULT_LAMP_SHORT) != 0;
  power->load.open = (diag->faults & LIGHT_FAULT_LAMP_OPEN) != 0;
  power->load.power_factor = (diag->faults & LIGHT_FAULT_OVERLOAD) ? LIGHT_ECU_OVERLOAD_FACTOR : 1.0;
  power->supply.open_circuit_v =
      ((diag->faults & LIGHT_FAULT_SUPPLY_LOW) ? LIGHT_ECU_SUPPLY_LOW_MV : LIGHT_ECU_SUPPLY_NORMAL_MV) / 1000.0;
}

/* The monitors of a real ECU: they only see the measured values, never the injected fault. */
static void run_monitors(LightEcuDiag* diag, uint32_t dt_ms) {
  const PowerReading* r = &diag->reading;
  const bool commanded = diag->ecu->lamp_on && diag->ecu->level > 0;
  const bool conducting = commanded && !r->tripped && !r->fuse_blown;
  const double nominal = power_path_nominal_current(&diag->power);

  if (r->tripped) {
    uds_dtc_report(&diag->dtcs[DTC_SHORT], true);
  } else if (conducting) {
    uds_dtc_report(&diag->dtcs[DTC_SHORT], false);
  }

  if (conducting) {
    /* The commanded duty: with an open load the model applies none. */
    const double duty = diag->ecu->level / 100.0;
    /* Open load: practically no current although the switch is on. */
    const bool open = r->current_a < 0.1 * nominal * duty;
    uds_dtc_report(&diag->dtcs[DTC_OPEN], open);
    /* Overcurrent: on-current (average / duty) above 1.5 x nominal for 200 ms. */
    const bool high = r->current_a / duty > LIGHT_ECU_OVERCURRENT_FACTOR * nominal;
    diag->overcurrent_ms = high ? diag->overcurrent_ms + dt_ms : 0;
    if (diag->overcurrent_ms >= LIGHT_ECU_OVERCURRENT_MS) {
      uds_dtc_report(&diag->dtcs[DTC_OVERCURRENT], true);
    } else if (!high) {
      uds_dtc_report(&diag->dtcs[DTC_OVERCURRENT], false);
    }
  } else {
    diag->overcurrent_ms = 0;
  }

  if (commanded) {
    uds_dtc_report(&diag->dtcs[DTC_FUSE], r->fuse_blown);
  }
  uds_dtc_report(&diag->dtcs[DTC_SUPPLY], r->supply_v < LIGHT_ECU_SUPPLY_THRESHOLD_V);
}

void light_ecu_diag_step(LightEcuDiag* diag, uint64_t now_ms) {
  const uint32_t dt_ms = diag->stepped && now_ms > diag->last_step_ms ? (uint32_t)(now_ms - diag->last_step_ms) : 0u;
  diag->last_step_ms = now_ms;
  diag->stepped = true;

  apply_faults(diag);
  power_path_step(&diag->power, diag->ecu->lamp_on, diag->ecu->level / 100.0, dt_ms, &diag->reading);
  /* What LAMP_CMD reports is the real output: off while protected or without fuse. */
  light_ecu_set_output_inhibit(diag->ecu, diag->reading.tripped || diag->reading.fuse_blown);
  run_monitors(diag, dt_ms);

  uds_server_poll(&diag->uds, now_ms * 1000u);
}

bool light_ecu_diag_status(LightEcuDiag* diag, uint64_t now_ms, CanFrame* out) {
  if (diag->last_status_ms != 0 && now_ms - diag->last_status_ms < LIGHT_ECU_STATUS_PERIOD_MS) {
    return false;
  }
  diag->last_status_ms = now_ms != 0 ? now_ms : 1u;
  const PowerReading* r = &diag->reading;
  uint32_t current_ma = round_milli(r->current_a);
  uint32_t supply_mv = round_milli(r->supply_v);
  current_ma = current_ma > 0xFFFFu ? 0xFFFFu : current_ma;
  supply_mv = supply_mv > 0xFFFFu ? 0xFFFFu : supply_mv;
  uint8_t flags = 0;
  if (r->fuse_blown) {
    flags |= LIGHT_ECU_STATUS_FUSE_BLOWN;
  }
  if (diag->dtcs[DTC_OVERCURRENT].status & UDS_DTC_TEST_FAILED) {
    flags |= LIGHT_ECU_STATUS_OVERCURRENT;
  }
  if (r->tripped) {
    flags |= LIGHT_ECU_STATUS_SHORT_SHUTDOWN;
  }
  /* Intel byte order, see LAMP_STATUS in data/dbc/iotwin.dbc */
  const uint8_t payload[6] = {(uint8_t)current_ma, (uint8_t)(current_ma >> 8), (uint8_t)supply_mv,
                              (uint8_t)(supply_mv >> 8), (uint8_t)(r->duty * 100.0 + 0.5), flags};
  return can_frame_set(out, LIGHT_ECU_ID_LAMP_STATUS, false, payload, sizeof(payload));
}

void light_ecu_diag_set_fault(LightEcuDiag* diag, LightEcuFault fault, bool active) {
  if (active) {
    diag->faults = (uint8_t)(diag->faults | fault);
  } else {
    diag->faults = (uint8_t)(diag->faults & ~fault);
  }
}

void light_ecu_diag_replace_fuse(LightEcuDiag* diag) { power_path_replace_fuse(&diag->power); }

bool light_ecu_fault_from_name(const char* name, LightEcuFault* fault) {
  if (strcmp(name, "short") == 0) {
    *fault = LIGHT_FAULT_LAMP_SHORT;
  } else if (strcmp(name, "open") == 0) {
    *fault = LIGHT_FAULT_LAMP_OPEN;
  } else if (strcmp(name, "supply") == 0) {
    *fault = LIGHT_FAULT_SUPPLY_LOW;
  } else if (strcmp(name, "overload") == 0) {
    *fault = LIGHT_FAULT_OVERLOAD;
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
    case LIGHT_FAULT_OVERLOAD: return "lamp overload (three times the rated power)";
    default: return "unknown fault";
  }
}
