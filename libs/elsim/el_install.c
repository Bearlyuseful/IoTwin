#include "el_install.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "json.h"

typedef struct {
  JsonValue** documents;
  size_t count;
} Catalog;

static void catalog_free(Catalog* catalog) {
  for (size_t i = 0; i < catalog->count; i++) {
    json_free(catalog->documents[i]);
  }
  free(catalog->documents);
  catalog->documents = NULL;
  catalog->count = 0;
}

static bool ends_with(const char* text, const char* suffix) {
  const size_t a = strlen(text);
  const size_t b = strlen(suffix);
  return a >= b && strcmp(text + a - b, suffix) == 0;
}

/* Reads every *.json below dir (unreadable files are skipped; the validator reports them). */
static void catalog_scan(Catalog* catalog, const char* dir, int depth) {
  DIR* handle = depth < 8 ? opendir(dir) : NULL;
  if (handle == NULL) {
    return;
  }
  struct dirent* entry;
  while ((entry = readdir(handle)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    struct stat info;
    if (stat(path, &info) != 0) {
      continue;
    }
    if (S_ISDIR(info.st_mode)) {
      catalog_scan(catalog, path, depth + 1);
    } else if (ends_with(entry->d_name, ".json")) {
      char error[160];
      JsonValue* document = json_parse_file(path, error, sizeof(error));
      if (document == NULL) {
        continue;
      }
      JsonValue** grown = realloc(catalog->documents, (catalog->count + 1) * sizeof(JsonValue*));
      if (grown == NULL) {
        json_free(document);
        continue;
      }
      catalog->documents = grown;
      catalog->documents[catalog->count++] = document;
    }
  }
  closedir(handle);
}

static const JsonValue* catalog_find(const Catalog* catalog, const char* id) {
  for (size_t i = 0; i < catalog->count; i++) {
    const char* candidate = json_get_string(catalog->documents[i], "id", NULL);
    if (candidate != NULL && strcmp(candidate, id) == 0) {
      return catalog->documents[i];
    }
  }
  return NULL;
}

/* Value of a component parameter: value, else typ (null or missing = not set). */
static bool component_parameter(const JsonValue* component, const char* name, double* out) {
  const JsonValue* parameter = json_get(json_get(component, "parameters"), name);
  const JsonValue* value = json_get(parameter, "value");
  if (value == NULL || value->type != JSON_NUMBER) {
    value = json_get(parameter, "typ");
  }
  if (value == NULL || value->type != JSON_NUMBER) {
    return false;
  }
  *out = value->number;
  return true;
}

/* Applies one named parameter from a component (source = component) or a device (source = device JSON). */
static void apply(ElDevice* d, const JsonValue* component, const JsonValue* device) {
  struct {
    const char* name;
    double* field;
  } const map[] = {
      {"rated_current", &d->rated_current_a},
      {"magnetic_trip_multiple", &d->magnetic_multiple},
      {"thermal_limit", &d->thermal_limit},
      {"melting_i2t", &d->thermal_limit},
      {"rated_residual_current", &d->residual_rated_a},
      {"rated_power", &d->rated_power_w},
      {"rated_voltage", &d->voltage_v},
      {"contact_resistance", &d->contact_ohm},
      {"voltage", &d->voltage_v},
      {"frequency", &d->frequency_hz},
      {"loop_impedance", &d->loop_impedance_ohm},
      {"earth_electrode", &d->earth_electrode_ohm},
      {"source_earth", &d->source_earth_ohm},
  };
  if (component != NULL) {
    const char* characteristic = json_get_string(json_get(component, "properties"), "characteristic", NULL);
    if (characteristic != NULL && d->type == EL_BREAKER) {
      d->magnetic_multiple = characteristic[0] == 'C' ? 10.0 : characteristic[0] == 'D' ? 20.0 : 5.0;
    }
  }
  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
    double value;
    if (component != NULL && component_parameter(component, map[i].name, &value)) {
      *map[i].field = value;
    }
    const JsonValue* own = json_get(device, map[i].name);
    if (own != NULL && own->type == JSON_NUMBER) {
      *map[i].field = own->number;
    }
  }
}

static bool fail(char* error, size_t size, const char* format, const char* a, const char* b) {
  snprintf(error, size, format, a ? a : "", b ? b : "");
  return false;
}

static bool load_document(ElModel* model, const JsonValue* root, const Catalog* catalog, char* error, size_t size) {
  if (root == NULL || root->type != JSON_OBJECT) {
    return fail(error, size, "installation must be a JSON object%s%s", NULL, NULL);
  }
  snprintf(model->id, sizeof(model->id), "%s", json_get_string(root, "id", ""));
  snprintf(model->name, sizeof(model->name), "%s", json_get_string(root, "name", model->id));

  const JsonValue* cables = json_get(root, "cables");
  for (size_t i = 0; i < json_size(cables); i++) {
    const JsonValue* c = json_at(cables, i);
    const char* id = json_get_string(c, "id", "");
    if (el_model_add_cable(model, id, json_get_string(c, "type", ""), json_get_number(c, "cross_section", 0.0),
                           json_get_number(c, "length", -1.0)) < 0) {
      return fail(error, size, "cable '%s': needs a unique id, cross_section > 0 and length >= 0%s", id, NULL);
    }
  }

  const JsonValue* devices = json_get(root, "devices");
  for (size_t i = 0; i < json_size(devices); i++) {
    const JsonValue* j = json_at(devices, i);
    const char* id = json_get_string(j, "id", "");
    const char* type_name = json_get_string(j, "type", "");
    ElDeviceType type;
    if (!el_device_type_from_name(type_name, &type)) {
      return fail(error, size, "device '%s': unknown type '%s'", id, type_name);
    }
    const int index = el_model_add_device(model, id, type);
    if (index < 0) {
      return fail(error, size, "device '%s': id missing or used twice%s", id, NULL);
    }
    ElDevice* d = &model->devices[index];
    const char* component_id = json_get_string(j, "component", NULL);
    const JsonValue* component = NULL;
    if (component_id != NULL) {
      component = catalog_find(catalog, component_id);
      if (component == NULL) {
        return fail(error, size, "device '%s': component '%s' not found in the component database", id, component_id);
      }
      snprintf(d->component, sizeof(d->component), "%s", component_id);
    }
    snprintf(d->label, sizeof(d->label), "%s", json_get_string(j, "label", ""));
    snprintf(d->room, sizeof(d->room), "%s", json_get_string(j, "room", ""));
    apply(d, component, j);
    d->on = json_get_bool(j, "on", d->on);
    const char* system = json_get_string(j, "system", NULL);
    if (system != NULL) {
      if (strcmp(system, "TN-S") == 0) {
        d->earthing = EL_TN_S;
      } else if (strcmp(system, "TN-C-S") == 0) {
        d->earthing = EL_TN_C_S;
      } else if (strcmp(system, "TT") == 0) {
        d->earthing = EL_TT;
      } else {
        return fail(error, size, "device '%s': unknown earthing system '%s' (TN-S, TN-C-S, TT)", id, system);
      }
    }
  }

  /* Plugs after all devices exist. */
  for (size_t i = 0; i < json_size(devices); i++) {
    const JsonValue* j = json_at(devices, i);
    const char* socket = json_get_string(j, "plugged_into", NULL);
    if (socket != NULL && !el_model_plug(model, json_get_string(j, "id", ""), socket)) {
      return fail(error, size, "device '%s': plugged_into '%s' must be a socket and the device a load",
                  json_get_string(j, "id", ""), socket);
    }
  }

  const JsonValue* connections = json_get(root, "connections");
  for (size_t i = 0; i < json_size(connections); i++) {
    const JsonValue* c = json_at(connections, i);
    const char* from = json_get_string(c, "from", "");
    const char* to = json_get_string(c, "to", "");
    if (el_model_connect(model, json_get_string(c, "id", NULL), from, to, json_get_string(c, "role", ""),
                         json_get_string(c, "cable", NULL)) < 0) {
      return fail(error, size, "connection %s -> %s: unknown terminal, unknown cable or duplicate id", from, to);
    }
  }
  if (model->device_count == 0) {
    return fail(error, size, "installation has no devices%s%s", NULL, NULL);
  }
  return true;
}

static bool load(ElModel* model, JsonValue* root, const char* components_dir, char* error, size_t size) {
  Catalog catalog = {NULL, 0};
  if (components_dir != NULL) {
    catalog_scan(&catalog, components_dir, 0);
  }
  const bool ok = load_document(model, root, &catalog, error, size);
  catalog_free(&catalog);
  json_free(root);
  return ok;
}

bool el_load_installation(ElModel* model, const char* path, const char* components_dir, char* error,
                          size_t error_size) {
  JsonValue* root = json_parse_file(path, error, error_size);
  if (root == NULL) {
    return false;
  }
  char message[256];
  if (!load(model, root, components_dir, message, sizeof(message))) {
    snprintf(error, error_size, "%s: %s", path, message);
    return false;
  }
  return true;
}

bool el_load_installation_text(ElModel* model, const char* text, const char* components_dir, char* error,
                               size_t error_size) {
  JsonValue* root = json_parse(text, error, error_size);
  return root != NULL && load(model, root, components_dir, error, error_size);
}
