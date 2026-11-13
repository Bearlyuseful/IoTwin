#include "line_reader.h"

void line_reader_init(LineReader* reader) {
  reader->length = 0;
  reader->discarding = false;
  reader->dropped_lines = 0;
  reader->buffer[0] = '\0';
}

size_t line_reader_feed(LineReader* reader, const char* data, size_t length,
                        LineReaderCallback callback, void* user_data) {
  size_t lines = 0;
  for (size_t i = 0; i < length; i++) {
    const char c = data[i];
    if (c == '\n') {
      if (reader->discarding) {
        reader->discarding = false;
        reader->dropped_lines++;
      } else {
        size_t line_length = reader->length;
        if (line_length > 0 && reader->buffer[line_length - 1] == '\r') {
          line_length--;
        }
        reader->buffer[line_length] = '\0';
        if (line_length > 0 && callback != NULL) {
          callback(reader->buffer, line_length, user_data);
          lines++;
        }
      }
      reader->length = 0;
      continue;
    }
    if (reader->discarding) {
      continue;
    }
    if (reader->length < LINE_READER_MAX_LINE - 1) {
      reader->buffer[reader->length++] = c;
    } else {
      reader->discarding = true;
      reader->length = 0;
    }
  }
  return lines;
}
