#include "light_ecu.h"

void light_ecu_init(LightEcu* ecu, uint32_t cycle_ms) {
  ecu->lamp_on = false;
  ecu->cycle_ms = cycle_ms;
  ecu->blink_period_ms = 0;
  ecu->last_send_ms = 0;
  ecu->last_toggle_ms = 0;
  ecu->dirty = true;
  ecu->started = false;
}

bool light_ecu_on_frame(LightEcu* ecu, const CanFrame* frame) {
  if (frame->extended || frame->rtr || frame->id != LIGHT_ECU_ID_SWITCH_STATE || frame->dlc < 1) {
    return false;
  }
  light_ecu_set_lamp(ecu, (frame->data[0] & 0x01u) != 0);
  return true;
}

void light_ecu_set_lamp(LightEcu* ecu, bool on) {
  ecu->blink_period_ms = 0;
  if (ecu->lamp_on != on) {
    ecu->lamp_on = on;
    ecu->dirty = true;
  }
}

void light_ecu_set_blink(LightEcu* ecu, uint32_t period_ms) {
  ecu->blink_period_ms = period_ms;
  ecu->started = ecu->started && period_ms == 0; /* restart the blink phase on the next step */
}

void light_ecu_request_send(LightEcu* ecu) { ecu->dirty = true; }

bool light_ecu_step(LightEcu* ecu, uint64_t now_ms, CanFrame* out, bool* state_changed) {
  bool changed = false;
  if (!ecu->started) {
    ecu->started = true;
    ecu->last_toggle_ms = now_ms;
  }

  if (ecu->blink_period_ms > 0 && now_ms - ecu->last_toggle_ms >= ecu->blink_period_ms) {
    ecu->lamp_on = !ecu->lamp_on;
    ecu->last_toggle_ms = now_ms;
    ecu->dirty = true;
  }

  bool send = ecu->dirty;
  if (send) {
    changed = true;
  } else if (ecu->cycle_ms > 0 && now_ms - ecu->last_send_ms >= ecu->cycle_ms) {
    send = true;
  }

  if (state_changed != NULL) {
    *state_changed = changed;
  }
  if (!send) {
    return false;
  }

  const uint8_t payload = ecu->lamp_on ? 0x01u : 0x00u;
  can_frame_set(out, LIGHT_ECU_ID_LAMP_CMD, false, &payload, 1);
  ecu->last_send_ms = now_ms;
  ecu->dirty = false;
  return true;
}
