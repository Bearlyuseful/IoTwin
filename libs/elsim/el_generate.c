#include "el_generate.h"

#include <stdio.h>

ElHouseSpec el_house_spec_default(void) {
  ElHouseSpec spec;
  spec.circuits = 24;
  spec.boxes = 6;
  spec.circuits_per_rcd = 6;
  spec.seed = 1;
  return spec;
}

/* Small reproducible generator (LCG), independent of the C library's rand(). */
static unsigned next_random(unsigned* state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

static double uniform(unsigned* state, double lo, double hi) {
  return lo + (hi - lo) * (double)(next_random(state) % 10000u) / 10000.0;
}

static bool connect3(ElModel* m, const char* a, const char* b, const char* cable) {
  static const char* const roles[3] = {"L", "N", "PE"};
  char from[EL_ID_MAX + 8];
  char to[EL_ID_MAX + 8];
  for (int i = 0; i < 3; i++) {
    snprintf(from, sizeof(from), "%s:%s", a, roles[i]);
    snprintf(to, sizeof(to), "%s:%s", b, roles[i]);
    if (el_model_connect(m, NULL, from, to, roles[i], cable) < 0) {
      return false;
    }
  }
  return true;
}

static bool add_load(ElModel* m, const char* id, double watts) {
  const int index = el_model_add_device(m, id, EL_LOAD);
  if (index < 0) {
    return false;
  }
  m->devices[index].rated_power_w = watts;
  return true;
}

bool el_generate_house(ElModel* m, const ElHouseSpec* spec) {
  unsigned random = spec->seed ? spec->seed : 1u;
  const int per_rcd = spec->circuits_per_rcd > 0 ? spec->circuits_per_rcd : 6;
  char id[EL_ID_MAX];
  char a[EL_ID_MAX + 8];
  char b[EL_ID_MAX + 8];
  char cable[EL_ID_MAX];
  char box[EL_ID_MAX];
  char previous[EL_ID_MAX];

  const int supply = el_model_add_device(m, "intake", EL_SUPPLY);
  if (supply < 0) {
    return false;
  }
  m->devices[supply].loop_impedance_ohm = 0.3;

  for (int c = 0; c < spec->circuits; c++) {
    const int group = c / per_rcd;
    const int phase = group % 3;
    const bool lighting = c % 2 == 0;
    char rcd[EL_ID_MAX];
    snprintf(rcd, sizeof(rcd), "q%d", group);
    if (c % per_rcd == 0) {
      if (el_model_add_device(m, rcd, EL_RCD) < 0) {
        return false;
      }
      snprintf(a, sizeof(a), "intake:L%d", phase + 1);
      snprintf(b, sizeof(b), "%s:L_in", rcd);
      bool ok = el_model_connect(m, NULL, a, b, "L", NULL) >= 0;
      snprintf(b, sizeof(b), "%s:N_in", rcd);
      ok = ok && el_model_connect(m, NULL, "intake:N", b, "N", NULL) >= 0;
      if (!ok) {
        return false;
      }
    }

    char breaker[EL_ID_MAX];
    snprintf(breaker, sizeof(breaker), "f%d", c);
    const int f = el_model_add_device(m, breaker, EL_BREAKER);
    if (f < 0) {
      return false;
    }
    m->devices[f].rated_current_a = lighting ? 10.0 : 16.0;
    snprintf(a, sizeof(a), "%s:L_out", rcd);
    snprintf(b, sizeof(b), "%s:in", breaker);
    if (el_model_connect(m, NULL, a, b, "L", NULL) < 0) {
      return false;
    }

    bool kettle = false;
    for (int k = 0; k < spec->boxes; k++) {
      snprintf(box, sizeof(box), "c%db%d", c, k);
      snprintf(cable, sizeof(cable), "w%db%d", c, k);
      if (el_model_add_device(m, box, EL_JUNCTION) < 0 ||
          el_model_add_cable(m, cable, lighting ? "NYM-J 3x1.5" : "NYM-J 3x2.5", lighting ? 1.5 : 2.5,
                             uniform(&random, 3.0, 15.0)) < 0) {
        return false;
      }
      bool ok;
      if (k == 0) {
        /* Board to the first box: L from the breaker, N from the RCD, PE from the main earthing bar. */
        snprintf(a, sizeof(a), "%s:out", breaker);
        snprintf(b, sizeof(b), "%s:L", box);
        ok = el_model_connect(m, NULL, a, b, "L", cable) >= 0;
        snprintf(a, sizeof(a), "%s:N_out", rcd);
        snprintf(b, sizeof(b), "%s:N", box);
        ok = ok && el_model_connect(m, NULL, a, b, "N", cable) >= 0;
        snprintf(b, sizeof(b), "%s:PE", box);
        ok = ok && el_model_connect(m, NULL, "intake:PE", b, "PE", cable) >= 0;
      } else {
        ok = connect3(m, previous, box, cable);
      }
      if (!ok) {
        return false;
      }

      if (lighting) {
        /* box L -> switch -> lamp L; N and PE straight to the lamp */
        char sw[EL_ID_MAX];
        snprintf(sw, sizeof(sw), "s%db%d", c, k);
        snprintf(id, sizeof(id), "l%db%d", c, k);
        const int s = el_model_add_device(m, sw, EL_SWITCH);
        if (s < 0 || !add_load(m, id, 60.0)) {
          return false;
        }
        m->devices[s].on = true;
        snprintf(a, sizeof(a), "%s:L", box);
        snprintf(b, sizeof(b), "%s:in", sw);
        ok = el_model_connect(m, NULL, a, b, "L", NULL) >= 0;
        snprintf(a, sizeof(a), "%s:out", sw);
        snprintf(b, sizeof(b), "%s:L", id);
        ok = ok && el_model_connect(m, NULL, a, b, "L", NULL) >= 0;
        snprintf(a, sizeof(a), "%s:N", box);
        snprintf(b, sizeof(b), "%s:N", id);
        ok = ok && el_model_connect(m, NULL, a, b, "N", NULL) >= 0;
        snprintf(a, sizeof(a), "%s:PE", box);
        snprintf(b, sizeof(b), "%s:PE", id);
        ok = ok && el_model_connect(m, NULL, a, b, "PE", NULL) >= 0;
      } else {
        char socket[EL_ID_MAX];
        snprintf(socket, sizeof(socket), "k%db%d", c, k);
        ok = el_model_add_device(m, socket, EL_SOCKET) >= 0 && connect3(m, box, socket, NULL);
        /* None 40 %, PC, fridge, TV, at most one kettle per circuit. */
        const unsigned pick = next_random(&random) % 100u;
        double watts = pick < 40 ? 0.0 : pick < 65 ? 150.0 : pick < 80 ? 120.0 : pick < 90 ? 100.0 : 2000.0;
        if (watts == 2000.0 && kettle) {
          watts = 150.0;
        }
        kettle = kettle || watts == 2000.0;
        if (ok && watts > 0.0) {
          snprintf(id, sizeof(id), "a%db%d", c, k);
          ok = add_load(m, id, watts) && el_model_plug(m, id, socket);
        }
      }
      if (!ok) {
        return false;
      }
      snprintf(previous, sizeof(previous), "%s", box);
    }
  }
  return true;
}
