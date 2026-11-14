#include "json.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char* text;
  size_t pos;
  int depth;
  char* error;
  size_t error_size;
  bool failed;
} Parser;

static void fail(Parser* p, const char* format, ...) {
  if (p->failed) {
    return;
  }
  p->failed = true;
  if (p->error == NULL || p->error_size == 0) {
    return;
  }
  size_t line = 1;
  size_t column = 1;
  for (size_t i = 0; i < p->pos && p->text[i] != '\0'; i++) {
    if (p->text[i] == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  char message[160];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  snprintf(p->error, p->error_size, "line %zu, column %zu: %s", line, column, message);
}

static void skip_space(Parser* p) {
  for (;;) {
    const char c = p->text[p->pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      p->pos++;
    } else {
      return;
    }
  }
}

static void free_contents(JsonValue* value) {
  if (value == NULL) {
    return;
  }
  free(value->string);
  for (size_t i = 0; i < value->count; i++) {
    free_contents(&value->items[i]);
    if (value->keys != NULL) {
      free(value->keys[i]);
    }
  }
  free(value->items);
  free(value->keys);
  memset(value, 0, sizeof(*value));
}

void json_free(JsonValue* value) {
  free_contents(value);
  free(value);
}

/* Growable byte buffer for strings. */
typedef struct {
  char* data;
  size_t length;
  size_t capacity;
} Buffer;

static bool buffer_put(Buffer* b, char c) {
  if (b->length + 2 > b->capacity) {
    const size_t capacity = b->capacity ? b->capacity * 2 : 32;
    char* grown = realloc(b->data, capacity);
    if (grown == NULL) {
      return false;
    }
    b->data = grown;
    b->capacity = capacity;
  }
  b->data[b->length++] = c;
  b->data[b->length] = '\0';
  return true;
}

static bool buffer_put_utf8(Buffer* b, uint32_t cp) {
  if (cp < 0x80) {
    return buffer_put(b, (char)cp);
  }
  if (cp < 0x800) {
    return buffer_put(b, (char)(0xC0 | (cp >> 6))) && buffer_put(b, (char)(0x80 | (cp & 0x3F)));
  }
  if (cp < 0x10000) {
    return buffer_put(b, (char)(0xE0 | (cp >> 12))) && buffer_put(b, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
           buffer_put(b, (char)(0x80 | (cp & 0x3F)));
  }
  return buffer_put(b, (char)(0xF0 | (cp >> 18))) && buffer_put(b, (char)(0x80 | ((cp >> 12) & 0x3F))) &&
         buffer_put(b, (char)(0x80 | ((cp >> 6) & 0x3F))) && buffer_put(b, (char)(0x80 | (cp & 0x3F)));
}

static int hex4(Parser* p) {
  int value = 0;
  for (int i = 0; i < 4; i++) {
    const char c = p->text[p->pos];
    int digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      fail(p, "invalid \\u escape");
      return -1;
    }
    value = (value << 4) | digit;
    p->pos++;
  }
  return value;
}

/* Parses a string starting at the opening quote; returns a heap copy. */
static char* parse_string_raw(Parser* p) {
  p->pos++; /* opening quote */
  Buffer b = {NULL, 0, 0};
  if (!buffer_put(&b, 'x')) {
    fail(p, "out of memory");
    return NULL;
  }
  b.length = 0;
  b.data[0] = '\0';
  for (;;) {
    const unsigned char c = (unsigned char)p->text[p->pos];
    if (c == '"') {
      p->pos++;
      return b.data;
    }
    if (c == '\0') {
      fail(p, "unterminated string");
      break;
    }
    if (c < 0x20) {
      fail(p, "control character in string");
      break;
    }
    if (c != '\\') {
      if (!buffer_put(&b, (char)c)) {
        fail(p, "out of memory");
        break;
      }
      p->pos++;
      continue;
    }
    p->pos++;
    const char escape = p->text[p->pos++];
    char plain = 0;
    switch (escape) {
      case '"': plain = '"'; break;
      case '\\': plain = '\\'; break;
      case '/': plain = '/'; break;
      case 'b': plain = '\b'; break;
      case 'f': plain = '\f'; break;
      case 'n': plain = '\n'; break;
      case 'r': plain = '\r'; break;
      case 't': plain = '\t'; break;
      case 'u': {
        int cp = hex4(p);
        if (cp < 0) {
          break;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          if (p->text[p->pos] != '\\' || p->text[p->pos + 1] != 'u') {
            fail(p, "unpaired surrogate");
            break;
          }
          p->pos += 2;
          const int low = hex4(p);
          if (low < 0xDC00 || low > 0xDFFF) {
            fail(p, "unpaired surrogate");
            break;
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          fail(p, "unpaired surrogate");
          break;
        }
        if (!buffer_put_utf8(&b, (uint32_t)cp)) {
          fail(p, "out of memory");
        }
        break;
      }
      default:
        p->pos--;
        fail(p, "invalid escape '\\%c'", escape);
        break;
    }
    if (p->failed) {
      break;
    }
    if (plain != 0 && !buffer_put(&b, plain)) {
      fail(p, "out of memory");
      break;
    }
  }
  free(b.data);
  return NULL;
}

static bool parse_value(Parser* p, JsonValue* out);

static bool append(JsonValue* container, JsonValue* item, char* key) {
  JsonValue* items = realloc(container->items, (container->count + 1) * sizeof(JsonValue));
  if (items == NULL) {
    return false;
  }
  container->items = items;
  if (container->type == JSON_OBJECT) {
    char** keys = realloc(container->keys, (container->count + 1) * sizeof(char*));
    if (keys == NULL) {
      return false;
    }
    container->keys = keys;
    container->keys[container->count] = key;
  }
  container->items[container->count++] = *item;
  return true;
}

static bool parse_container(Parser* p, JsonValue* out, bool object) {
  const char close = object ? '}' : ']';
  out->type = object ? JSON_OBJECT : JSON_ARRAY;
  if (++p->depth > JSON_MAX_DEPTH) {
    fail(p, "nesting deeper than %d", JSON_MAX_DEPTH);
    return false;
  }
  p->pos++;
  skip_space(p);
  if (p->text[p->pos] == close) {
    p->pos++;
    p->depth--;
    return true;
  }
  for (;;) {
    char* key = NULL;
    if (object) {
      skip_space(p);
      if (p->text[p->pos] != '"') {
        fail(p, "expected a member name in quotes");
        return false;
      }
      key = parse_string_raw(p);
      if (key == NULL) {
        return false;
      }
      skip_space(p);
      if (p->text[p->pos] != ':') {
        free(key);
        fail(p, "expected ':'");
        return false;
      }
      p->pos++;
    }
    JsonValue item;
    memset(&item, 0, sizeof(item));
    if (!parse_value(p, &item) || !append(out, &item, key)) {
      free_contents(&item);
      free(key);
      if (!p->failed) {
        fail(p, "out of memory");
      }
      return false;
    }
    skip_space(p);
    const char c = p->text[p->pos];
    if (c == ',') {
      p->pos++;
      continue;
    }
    if (c == close) {
      p->pos++;
      p->depth--;
      return true;
    }
    fail(p, object ? "expected ',' or '}'" : "expected ',' or ']'");
    return false;
  }
}

static bool parse_number(Parser* p, JsonValue* out) {
  const char* start = p->text + p->pos;
  const char* s = start;
  if (*s == '-') {
    s++;
  }
  if (*s == '0') {
    s++;
  } else if (*s >= '1' && *s <= '9') {
    while (*s >= '0' && *s <= '9') {
      s++;
    }
  } else {
    fail(p, "invalid value");
    return false;
  }
  if (*s == '.') {
    s++;
    if (!(*s >= '0' && *s <= '9')) {
      fail(p, "digits expected after '.'");
      return false;
    }
    while (*s >= '0' && *s <= '9') {
      s++;
    }
  }
  if (*s == 'e' || *s == 'E') {
    s++;
    if (*s == '+' || *s == '-') {
      s++;
    }
    if (!(*s >= '0' && *s <= '9')) {
      fail(p, "digits expected in the exponent");
      return false;
    }
    while (*s >= '0' && *s <= '9') {
      s++;
    }
  }
  errno = 0;
  out->type = JSON_NUMBER;
  out->number = strtod(start, NULL);
  if (errno == ERANGE && isinf(out->number)) {
    fail(p, "number out of range");
    return false;
  }
  p->pos += (size_t)(s - start);
  return true;
}

static bool literal(Parser* p, const char* word) {
  const size_t length = strlen(word);
  if (strncmp(p->text + p->pos, word, length) != 0) {
    fail(p, "invalid value");
    return false;
  }
  p->pos += length;
  return true;
}

static bool parse_value(Parser* p, JsonValue* out) {
  skip_space(p);
  const char c = p->text[p->pos];
  switch (c) {
    case '{':
      return parse_container(p, out, true);
    case '[':
      return parse_container(p, out, false);
    case '"':
      out->type = JSON_STRING;
      out->string = parse_string_raw(p);
      return out->string != NULL;
    case 't':
      out->type = JSON_BOOL;
      out->boolean = true;
      return literal(p, "true");
    case 'f':
      out->type = JSON_BOOL;
      out->boolean = false;
      return literal(p, "false");
    case 'n':
      out->type = JSON_NULL;
      return literal(p, "null");
    case '\0':
      fail(p, "unexpected end of input");
      return false;
    default:
      return parse_number(p, out);
  }
}

JsonValue* json_parse(const char* text, char* error, size_t error_size) {
  Parser p = {text, 0, 0, error, error_size, false};
  if (error != NULL && error_size > 0) {
    error[0] = '\0';
  }
  JsonValue* root = calloc(1, sizeof(JsonValue));
  if (root == NULL) {
    fail(&p, "out of memory");
    return NULL;
  }
  if (!parse_value(&p, root)) {
    json_free(root);
    return NULL;
  }
  skip_space(&p);
  if (p.text[p.pos] != '\0') {
    fail(&p, "unexpected text after the value");
    json_free(root);
    return NULL;
  }
  return root;
}

JsonValue* json_parse_file(const char* path, char* error, size_t error_size) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    snprintf(error, error_size, "cannot open %s", path);
    return NULL;
  }
  size_t capacity = 4096;
  size_t length = 0;
  char* text = malloc(capacity);
  size_t got;
  while (text != NULL && (got = fread(text + length, 1, capacity - length - 1, file)) > 0) {
    length += got;
    if (length + 1 == capacity) {
      char* grown = realloc(text, capacity * 2);
      if (grown == NULL) {
        free(text);
        text = NULL;
        break;
      }
      text = grown;
      capacity *= 2;
    }
  }
  fclose(file);
  if (text == NULL) {
    snprintf(error, error_size, "out of memory reading %s", path);
    return NULL;
  }
  text[length] = '\0';
  /* Skip a UTF-8 byte order mark. */
  const char* start = (length >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
                       (unsigned char)text[2] == 0xBF)
                          ? text + 3
                          : text;
  char message[200];
  JsonValue* root = json_parse(start, message, sizeof(message));
  if (root == NULL) {
    snprintf(error, error_size, "%s: %s", path, message);
  }
  free(text);
  return root;
}

const JsonValue* json_get(const JsonValue* object, const char* key) {
  if (object == NULL || object->type != JSON_OBJECT) {
    return NULL;
  }
  for (size_t i = 0; i < object->count; i++) {
    if (strcmp(object->keys[i], key) == 0) {
      return &object->items[i];
    }
  }
  return NULL;
}

const JsonValue* json_at(const JsonValue* array, size_t index) {
  if (array == NULL || array->type != JSON_ARRAY || index >= array->count) {
    return NULL;
  }
  return &array->items[index];
}

size_t json_size(const JsonValue* value) {
  return (value != NULL && (value->type == JSON_ARRAY || value->type == JSON_OBJECT)) ? value->count : 0;
}

const char* json_get_string(const JsonValue* object, const char* key, const char* fallback) {
  const JsonValue* v = json_get(object, key);
  return (v != NULL && v->type == JSON_STRING) ? v->string : fallback;
}

double json_get_number(const JsonValue* object, const char* key, double fallback) {
  const JsonValue* v = json_get(object, key);
  return (v != NULL && v->type == JSON_NUMBER) ? v->number : fallback;
}

bool json_get_bool(const JsonValue* object, const char* key, bool fallback) {
  const JsonValue* v = json_get(object, key);
  return (v != NULL && v->type == JSON_BOOL) ? v->boolean : fallback;
}
