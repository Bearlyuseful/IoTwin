#ifndef IOTWIN_JSON_H
#define IOTWIN_JSON_H

/**
 * @file json.h
 * @brief Small JSON reader (RFC 8259) for the engineering data in data/.
 *
 * Parses a whole document into a tree. Objects keep the order of their
 * members; duplicate keys are kept (json_get returns the first). Strings are
 * UTF-8, \u escapes incl. surrogate pairs are decoded. Nesting is limited to
 * JSON_MAX_DEPTH. Host only (uses the heap).
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_MAX_DEPTH 64

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
  JsonType type;
  bool boolean;
  double number;
  char* string;      /**< JSON_STRING: NUL terminated UTF-8. */
  JsonValue* items;  /**< JSON_ARRAY elements or JSON_OBJECT member values. */
  char** keys;       /**< JSON_OBJECT member names (count entries). */
  size_t count;
};

/**
 * @brief Parses @p text (NUL terminated).
 * @return The root value (free with json_free), or NULL with a message
 *         "line L, column C: ..." in @p error.
 */
JsonValue* json_parse(const char* text, char* error, size_t error_size);

/** @brief Reads and parses a file. */
JsonValue* json_parse_file(const char* path, char* error, size_t error_size);

void json_free(JsonValue* value);

/** @brief Member of an object, NULL if missing or @p object is not an object. */
const JsonValue* json_get(const JsonValue* object, const char* key);

/** @brief Element of an array, NULL if out of range. */
const JsonValue* json_at(const JsonValue* array, size_t index);

/** @brief Number of elements (array) or members (object), 0 otherwise. */
size_t json_size(const JsonValue* value);

/** @name Typed member access with fallback */
/**@{*/
const char* json_get_string(const JsonValue* object, const char* key, const char* fallback);
double json_get_number(const JsonValue* object, const char* key, double fallback);
bool json_get_bool(const JsonValue* object, const char* key, bool fallback);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_JSON_H */
