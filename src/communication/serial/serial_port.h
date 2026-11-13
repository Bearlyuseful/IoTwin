#ifndef IOTWIN_SERIAL_PORT_H
#define IOTWIN_SERIAL_PORT_H

/**
 * @file serial_port.h
 * @brief Minimal portable serial port (Win32 COM ports / POSIX termios).
 *
 * 8N1, no flow control, raw bytes. Port names: "COM5" on Windows,
 * "/dev/ttyACM0" (or a pseudo terminal) on Linux.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SerialPort SerialPort;

/** @brief Opens and configures a port. Returns NULL on failure (see serial_last_error()). */
SerialPort* serial_open(const char* name, uint32_t baudrate);

/**
 * @brief Reads available bytes, waiting at most @p timeout_ms for the first one.
 * @return Number of bytes (> 0), 0 on timeout, -1 on error (port gone).
 */
int serial_read(SerialPort* port, void* buffer, size_t size, int timeout_ms);

/** @brief Writes all bytes. Returns 0 on success, -1 on error. */
int serial_write_all(SerialPort* port, const void* data, size_t length);

void serial_close(SerialPort* port);

/** @brief Text of the last error of the calling thread. */
const char* serial_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_SERIAL_PORT_H */
