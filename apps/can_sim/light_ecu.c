#include "light_ecu.h"

void light_ecu_init(LightEcu* ecu, uint32_t cycle_ms) {
  ecu->lamp_on = false;
  ecu->switch_on = false;
  ecu->output_inhibit = false;
  ecu->level = LIGHT_ECU_DEFAULT_LEVEL;
  ecu->cycle_ms = cycle_ms;
  ecu->blink_period_ms = 0;
  ecu->last_send_ms = 0;
  ecu->last_toggle_ms = 0;
  ecu->dirty = true;
  ecu->started = false;
}

bool light_ecu_on_frame(LightEcu* ecu, const CanFrame* frame) {
  if (frame->extended || frame->rtr || frame->dlc < 1) {
    return false;
  }
  if (frame->id == LIGHT_ECU_ID_DIMMER_CMD) {
    light_ecu_set_level(ecu, frame->data[0]);
    return true;
  }
  if (frame->id != LIGHT_ECU_ID_SWITCH_STATE) {
    return false;
  }
  ecu->switch_on = (frame->data[0] & 0x01u) != 0;
  light_ecu_set_lamp(ecu, ecu->switch_on);
  return true;
}

void light_ecu_set_lamp(LightEcu* ecu, bool on) {
  ecu->blink_period_ms = 0;
  if (ecu->lamp_on != on) {
    ecu->lamp_on = on;
    ecu->dirty = true;
  }
}

void light_ecu_set_level(LightEcu* ecu, uint32_t percent) {
  const uint8_t level = (uint8_t)(percent > 100u ? 100u : percent);
  if (ecu->level != level) {
    ecu->level = level;
    ecu->dirty = true;
  }
}

void light_ecu_set_blink(LightEcu* ecu, uint32_t period_ms) {
  ecu->blink_period_ms = period_ms;
  ecu->started = ecu->started && period_ms == 0; /* restart the blink phase on the next step */
}

void light_ecu_set_output_inhibit(LightEcu* ecu, bool inhibit) {
  if (ecu->output_inhibit != inhibit) {
    ecu->output_inhibit = inhibit;
    ecu->dirty = ecu->dirty || ecu->lamp_on; /* the output changes only if the lamp is commanded on */
  }
}

bool light_ecu_output(const LightEcu* ecu) { return ecu->lamp_on && !ecu->output_inhibit; }

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

  const uint8_t payload[2] = {light_ecu_output(ecu) ? 0x01u : 0x00u, ecu->level};
  can_frame_set(out, LIGHT_ECU_ID_LAMP_CMD, false, payload, 2);
  ecu->last_send_ms = now_ms;
  ecu->dirty = false;
  return true;
}
