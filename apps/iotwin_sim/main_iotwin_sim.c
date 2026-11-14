/**
 * @file main_iotwin_sim.c
 * @brief iotwin-sim: electrical simulation of a house installation (ADR 0007).
 *
 *   iotwin-sim                                   demo flat, interactive
 *   iotwin-sim data/installations/demo-flat.json
 *   iotwin-sim --script tests/elsim/demo-flat.sim   commands from a file, 'expect' checks
 *
 * Every command that changes the installation lets 200 ms pass, so breakers,
 * fuses and RCDs can react; their events are printed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el_install.h"
#include "elsim.h"

#define STEP_S 0.01
#define SETTLE_S 0.2
#define MAX_ARGS 8

typedef struct {
  ElModel model;
  bool script;
  int failures;
  int line;
} Sim;

static void print_help(void) {
  printf("Commands:\n"
         "  state                       all devices: state, voltage, current, power\n"
         "  on <id> | off <id>          switch, breaker or RCD handle, appliance switch\n"
         "  reset <id>                  re-arm a breaker/RCD, replace a fuse\n"
         "  test <rcd>                  press the RCD test button\n"
         "  fault <a> <b> [ohm]         impedance between two terminals (default 0 = short circuit)\n"
         "  open <conn> | close <conn>  interrupt / restore a conductor\n"
         "  clear                       remove all faults\n"
         "  plug <load> <socket> | unplug <load>\n"
         "  u <a> <b>                   voltage between terminals\n"
         "  z <a> <b>                   impedance (loop impedance, continuity) and fault current\n"
         "  riso <a> <b>                insulation resistance (refused while live)\n"
         "  wait <ms>                   let time pass\n"
         "  expect <id> powered|unpowered|tripped|on|off\n"
         "  expect u|z <a> <b> <min> <max>\n"
         "  help | quit\n"
         "Terminals are written device:terminal, e.g. kettle:L or intake:PE.\n");
}

static void print_events(Sim* sim) {
  ElEvent event;
  while (el_model_next_event(&sim->model, &event)) {
    printf("  %7.2f s  %s\n", event.time_s, event.text);
  }
}

static void run(Sim* sim, double seconds) {
  for (double t = 0; t < seconds - 1e-9; t += STEP_S) {
    if (!el_model_step(&sim->model, STEP_S)) {
      printf("  network cannot be solved\n");
      break;
    }
  }
  print_events(sim);
}

static const char* state_text(const ElDevice* d) {
  switch (d->type) {
    case EL_LOAD:
      return !d->on ? "off" : el_device_powered(d) ? "powered" : "no supply";
    case EL_BREAKER:
    case EL_RCD:
      return d->tripped ? "TRIPPED" : d->on ? "on" : "off";
    case EL_FUSE:
      return d->tripped ? "BLOWN" : "ok";
    case EL_SWITCH:
      return d->on ? "on" : "off";
    default:
      return "";
  }
}

static void print_state(Sim* sim) {
  const ElModel* m = &sim->model;
  printf("t = %.2f s   %s\n", m->time_s, m->name);
  printf("%-16s %-8s %-10s %8s %8s %8s  %s\n", "device", "type", "state", "U [V]", "I [A]", "P [W]", "note");
  for (size_t i = 0; i < m->device_count; i++) {
    const ElDevice* d = &m->devices[i];
    char u[16] = "";
    char current[16] = "";
    char power[16] = "";
    char note[96] = "";
    if (d->type == EL_LOAD) {
      snprintf(u, sizeof(u), "%.1f", d->voltage_v_measured);
      snprintf(power, sizeof(power), "%.1f", d->power_w);
      if (d->plugged_into >= 0) {
        snprintf(note, sizeof(note), "plugged into %s", m->devices[d->plugged_into].id);
      }
    }
    if (d->type != EL_SOCKET) {
      snprintf(current, sizeof(current), "%.2f", d->current_a);
    }
    if (d->type == EL_RCD) {
      snprintf(note, sizeof(note), "residual %.1f mA", d->residual_a * 1000.0);
    } else if ((d->type == EL_BREAKER || d->type == EL_FUSE) && d->heat > 0.0) {
      snprintf(note, sizeof(note), "thermal %.0f %%", 100.0 * d->heat / d->thermal_limit);
    } else if (d->type == EL_SUPPLY) {
      snprintf(note, sizeof(note), "%s, Ze %.2f Ohm", d->earthing == EL_TT ? "TT" : d->earthing == EL_TN_C_S ? "TN-C-S" : "TN-S",
               d->loop_impedance_ohm);
    }
    printf("%-16s %-8s %-10s %8s %8s %8s  %s%s%s\n", d->id, el_device_type_name(d->type), state_text(d), u, current,
           power, d->label, (d->label[0] && note[0]) ? ", " : "", note);
  }
  for (size_t i = 0; i < m->fault_count; i++) {
    printf("fault: %s (%.2f A)\n", m->faults[i].description, m->faults[i].current_a);
  }
  for (size_t i = 0; i < m->connection_count; i++) {
    if (m->connections[i].open) {
      printf("open conductor: %s\n", m->connections[i].id);
    }
  }
}

static bool check(Sim* sim, bool ok, const char* what) {
  if (!ok) {
    sim->failures++;
    printf("  EXPECT FAILED (line %d): %s\n", sim->line, what);
  }
  return ok;
}

static void expect(Sim* sim, int argc, char** argv, const char* original) {
  if (argc == 3) {
    const int index = el_model_find_device(&sim->model, argv[1]);
    if (!check(sim, index >= 0, "unknown device")) {
      return;
    }
    const ElDevice* d = &sim->model.devices[index];
    bool ok;
    if (strcmp(argv[2], "powered") == 0) {
      ok = el_device_powered(d);
    } else if (strcmp(argv[2], "unpowered") == 0) {
      ok = !el_device_powered(d);
    } else if (strcmp(argv[2], "tripped") == 0) {
      ok = d->tripped;
    } else if (strcmp(argv[2], "on") == 0) {
      ok = d->on && !d->tripped;
    } else if (strcmp(argv[2], "off") == 0) {
      ok = !d->on || d->tripped;
    } else {
      check(sim, false, "state must be powered, unpowered, tripped, on or off");
      return;
    }
    check(sim, ok, original);
    return;
  }
  if (argc == 6 && (strcmp(argv[1], "u") == 0 || strcmp(argv[1], "z") == 0)) {
    ElPhasor value;
    const bool measured = argv[1][0] == 'u' ? el_model_measure_voltage(&sim->model, argv[2], argv[3], &value)
                                            : el_model_measure_impedance(&sim->model, argv[2], argv[3], &value);
    if (!check(sim, measured, "unknown terminal")) {
      return;
    }
    const double magnitude = el_abs(value);
    const bool ok = magnitude >= atof(argv[4]) && magnitude <= atof(argv[5]);
    if (!ok) {
      printf("  measured %.4g\n", magnitude);
    }
    check(sim, ok, original);
    return;
  }
  check(sim, false, "usage: expect <id> <state> | expect u|z <a> <b> <min> <max>");
}

static bool handle(Sim* sim, char* line) {
  char original[256];
  snprintf(original, sizeof(original), "%s", line);
  original[strcspn(original, "\r\n")] = '\0';
  char* argv[MAX_ARGS];
  int argc = 0;
  for (char* token = strtok(line, " \t\r\n"); token != NULL && argc < MAX_ARGS; token = strtok(NULL, " \t\r\n")) {
    argv[argc++] = token;
  }
  if (argc == 0 || argv[0][0] == '#') {
    return true;
  }
  if (sim->script) {
    printf("> %s\n", original);
  }
  ElModel* m = &sim->model;
  const char* cmd = argv[0];
  bool known = true;
  bool ok = true;
  if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
    return false;
  } else if (strcmp(cmd, "help") == 0) {
    print_help();
  } else if (strcmp(cmd, "state") == 0 || strcmp(cmd, "s") == 0) {
    print_state(sim);
  } else if ((strcmp(cmd, "on") == 0 || strcmp(cmd, "off") == 0) && argc == 2) {
    ok = el_model_set_on(m, argv[1], cmd[1] == 'n');
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "reset") == 0 && argc == 2) {
    ok = el_model_reset(m, argv[1]);
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "test") == 0 && argc == 2) {
    ok = el_model_press_test(m, argv[1]);
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "fault") == 0 && (argc == 3 || argc == 4)) {
    ok = el_model_add_fault(m, argv[1], argv[2], argc == 4 ? atof(argv[3]) : 0.0, NULL) >= 0;
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if ((strcmp(cmd, "open") == 0 || strcmp(cmd, "close") == 0) && argc == 2) {
    ok = el_model_set_connection_open(m, argv[1], cmd[0] == 'o');
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "clear") == 0) {
    el_model_clear_faults(m);
    run(sim, SETTLE_S);
  } else if (strcmp(cmd, "plug") == 0 && argc == 3) {
    ok = el_model_plug(m, argv[1], argv[2]);
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "unplug") == 0 && argc == 2) {
    ok = el_model_plug(m, argv[1], NULL);
    if (ok) {
      run(sim, SETTLE_S);
    }
  } else if (strcmp(cmd, "wait") == 0 && argc == 2) {
    run(sim, atof(argv[1]) / 1000.0);
  } else if (strcmp(cmd, "u") == 0 && argc == 3) {
    ElPhasor u;
    ok = el_model_measure_voltage(m, argv[1], argv[2], &u);
    if (ok) {
      printf("  U = %.1f V (%.0f deg)\n", el_abs(u), el_angle_deg(u));
    }
  } else if (strcmp(cmd, "z") == 0 && argc == 3) {
    ElPhasor z;
    ok = el_model_measure_impedance(m, argv[1], argv[2], &z);
    if (ok) {
      const double ohm = el_abs(z);
      printf("  Z = %.3f Ohm, prospective fault current at 230 V: %.0f A\n", ohm, ohm > 0 ? 230.0 / ohm : 0.0);
    }
  } else if (strcmp(cmd, "riso") == 0 && argc == 3) {
    double ohm = 0;
    if (el_model_measure_insulation(m, argv[1], argv[2], &ohm)) {
      if (ohm > 999e6) {
        printf("  R_iso > 999 MOhm\n");
      } else if (ohm > 1e6) {
        printf("  R_iso = %.1f MOhm\n", ohm / 1e6);
      } else {
        printf("  R_iso = %.3f kOhm  (a load or a fault is connected)\n", ohm / 1e3);
      }
    } else {
      printf("  refused: voltage present or unknown terminal - switch the circuit off first\n");
    }
  } else if (strcmp(cmd, "expect") == 0) {
    expect(sim, argc, argv, original);
  } else {
    known = false;
  }
  if (!known) {
    printf("  unknown or incomplete command '%s' (type 'help')\n", cmd);
  } else if (!ok) {
    printf("  not possible: unknown id or terminal, or not allowed for this device\n");
  }
  if (sim->script && (!known || !ok)) {
    sim->failures++;
  }
  return true;
}

static void print_usage(const char* program) {
  printf("Usage: %s [options] [installation.json]\n\n"
         "Electrical simulation of a house installation: protective devices, faults,\n"
         "measurements. Default installation: <data>/installations/demo-flat.json.\n\n"
         "Options:\n"
         "  --data DIR       data directory with installations/ and components/ (default: data)\n"
         "  --script FILE    run commands from a file; exit code 1 if an 'expect' fails\n"
         "  --help           show this help\n\n",
         program);
  print_help();
}

int main(int argc, char** argv) {
  const char* data_dir = "data";
  const char* script = NULL;
  const char* installation = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
      data_dir = argv[++i];
    } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
      script = argv[++i];
    } else if (argv[i][0] != '-' && installation == NULL) {
      installation = argv[i];
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", argv[i]);
      return 2;
    }
  }

  char default_path[512];
  if (installation == NULL) {
    snprintf(default_path, sizeof(default_path), "%s/installations/demo-flat.json", data_dir);
    installation = default_path;
  }
  char components[512];
  snprintf(components, sizeof(components), "%s/components", data_dir);

  static Sim sim;
  el_model_init(&sim.model);
  char error[512];
  if (!el_load_installation(&sim.model, installation, components, error, sizeof(error))) {
    fprintf(stderr, "%s\n", error);
    el_model_free(&sim.model);
    return 2;
  }
  run(&sim, SETTLE_S);

  FILE* input = stdin;
  if (script != NULL) {
    input = fopen(script, "r");
    if (input == NULL) {
      fprintf(stderr, "cannot open %s\n", script);
      el_model_free(&sim.model);
      return 2;
    }
    sim.script = true;
  } else {
    printf("iotwin-sim: %s (%zu devices). Type 'help' for commands.\n", sim.model.name, sim.model.device_count);
  }

  char line[256];
  while (true) {
    if (!sim.script) {
      printf("sim> ");
      fflush(stdout);
    }
    if (fgets(line, sizeof(line), input) == NULL) {
      break;
    }
    sim.line++;
    if (!handle(&sim, line)) {
      break;
    }
    fflush(stdout);
  }
  if (script != NULL) {
    fclose(input);
    printf("%s: %d failed\n", script, sim.failures);
  }
  el_model_free(&sim.model);
  return sim.failures > 0 ? 1 : 0;
}
