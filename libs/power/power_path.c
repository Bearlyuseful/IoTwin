#include "power_path.h"

#include <string.h>

void power_path_init(PowerPath* path) {
  memset(path, 0, sizeof(*path));
  path->supply.open_circuit_v = 12.6;
  path->supply.internal_ohm = 0.05;
  path->fuse.rated_a = 3.0;
  path->fuse.melting_i2t = 40.0;
  path->fuse.cooling_i2t = 5.0;
  path->output.current_limit_a = 15.0;
  path->output.restart_ms = 100;
  path->load.rated_power_w = 21.0;
  path->load.rated_voltage_v = 12.0;
  path->load.power_factor = 1.0;
  path->wire_ohm = 0.02;
}

double power_path_nominal_current(const PowerPath* path) {
  return path->load.rated_voltage_v > 0.0 ? path->load.rated_power_w / path->load.rated_voltage_v : 0.0;
}

void power_path_replace_fuse(PowerPath* path) {
  path->fuse.blown = false;
  path->fuse.heat = 0.0;
}

static void cool_fuse(Fuse* fuse, double dt_s) {
  fuse->heat -= fuse->cooling_i2t * dt_s;
  if (fuse->heat < 0.0) {
    fuse->heat = 0.0;
  }
}

void power_path_step(PowerPath* path, bool on, double duty, uint32_t dt_ms, PowerReading* reading) {
  const double dt_s = (double)dt_ms / 1000.0;
  const double u0 = path->supply.open_circuit_v;
  memset(reading, 0, sizeof(*reading));
  reading->supply_v = u0;

  if (duty < 0.0) {
    duty = 0.0;
  } else if (duty > 1.0) {
    duty = 1.0;
  }

  /* Protection: restart attempt after restart_ms; switching off resets it. */
  SmartSwitch* output = &path->output;
  if (output->tripped) {
    output->tripped_ms += dt_ms;
    if (!on || output->tripped_ms >= output->restart_ms) {
      output->tripped = false;
      output->tripped_ms = 0;
    }
  }

  const bool conducting = on && duty > 0.0 && !path->fuse.blown && !output->tripped && !path->load.open;
  if (!conducting) {
    cool_fuse(&path->fuse, dt_s);
    reading->tripped = output->tripped;
    reading->fuse_blown = path->fuse.blown;
    return;
  }

  double load_ohm = 0.0;
  if (!path->load.shorted) {
    const double power = path->load.rated_power_w * (path->load.power_factor > 0.0 ? path->load.power_factor : 1.0);
    load_ohm = power > 0.0 ? path->load.rated_voltage_v * path->load.rated_voltage_v / power : 1e9;
  }
  const double on_current = u0 / (path->supply.internal_ohm + path->wire_ohm + load_ohm);

  if (on_current > output->current_limit_a) {
    /* Shutdown within microseconds: no lasting current, the fuse does not notice. */
    output->tripped = true;
    output->tripped_ms = 0;
    cool_fuse(&path->fuse, dt_s);
    reading->tripped = true;
    reading->fuse_blown = path->fuse.blown;
    return;
  }

  const double average = duty * on_current;
  const double rms_squared = duty * on_current * on_current;
  Fuse* fuse = &path->fuse;
  const double rated_squared = fuse->rated_a * fuse->rated_a;
  if (rms_squared > rated_squared) {
    fuse->heat += (rms_squared - rated_squared) * dt_s;
    if (fuse->heat >= fuse->melting_i2t) {
      fuse->blown = true;
    }
  } else {
    cool_fuse(fuse, dt_s);
  }

  reading->current_a = average;
  reading->supply_v = u0 - average * path->supply.internal_ohm;
  reading->load_v = average * load_ohm;
  reading->power_w = rms_squared * load_ohm;
  reading->duty = duty;
  reading->fuse_blown = fuse->blown;
}
