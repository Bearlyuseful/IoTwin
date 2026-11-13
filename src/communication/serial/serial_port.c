#if !defined(_WIN32)
#define _DEFAULT_SOURCE /* cfmakeraw */
#endif

#include "serial_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

static _Thread_local char g_error[160];

const char* serial_last_error(void) { return g_error; }

#ifdef _WIN32

struct SerialPort {
  HANDLE handle;
  int read_timeout_ms;
};

static void remember_error(const char* what) {
  DWORD code = GetLastError();
  char text[120] = "";
  DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                                code, 0, text, sizeof(text), NULL);
  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == '.')) {
    text[--length] = '\0';
  }
  snprintf(g_error, sizeof(g_error), "%s: %s (%lu)", what, text, (unsigned long)code);
}

static int apply_read_timeout(SerialPort* port, int timeout_ms) {
  if (port->read_timeout_ms == timeout_ms) {
    return 0;
  }
  /* Return as soon as at least one byte is available, or after timeout_ms. */
  COMMTIMEOUTS timeouts;
  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = timeout_ms > 0 ? (DWORD)timeout_ms : 1;
  timeouts.WriteTotalTimeoutConstant = 1000;
  if (!SetCommTimeouts(port->handle, &timeouts)) {
    remember_error("SetCommTimeouts");
    return -1;
  }
  port->read_timeout_ms = timeout_ms;
  return 0;
}

SerialPort* serial_open(const char* name, uint32_t baudrate) {
  char path[64];
  if (strncmp(name, "\\\\.\\", 4) == 0) {
    snprintf(path, sizeof(path), "%s", name);
  } else {
    snprintf(path, sizeof(path), "\\\\.\\%s", name); /* required for COM10 and above */
  }

  HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    remember_error(name);
    return NULL;
  }

  DCB dcb;
  memset(&dcb, 0, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(handle, &dcb)) {
    remember_error("GetCommState");
    CloseHandle(handle);
    return NULL;
  }
  dcb.BaudRate = baudrate;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fNull = FALSE;
  dcb.fAbortOnError = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(handle, &dcb)) {
    remember_error("SetCommState");
    CloseHandle(handle);
    return NULL;
  }

  SerialPort* port = calloc(1, sizeof(SerialPort));
  if (port == NULL) {
    CloseHandle(handle);
    return NULL;
  }
  port->handle = handle;
  port->read_timeout_ms = -1;
  if (apply_read_timeout(port, 10) != 0) {
    serial_close(port);
    return NULL;
  }
  PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return port;
}

int serial_read(SerialPort* port, void* buffer, size_t size, int timeout_ms) {
  if (apply_read_timeout(port, timeout_ms) != 0) {
    return -1;
  }
  DWORD received = 0;
  if (!ReadFile(port->handle, buffer, (DWORD)size, &received, NULL)) {
    remember_error("ReadFile");
    return -1;
  }
  return (int)received;
}

int serial_write_all(SerialPort* port, const void* data, size_t length) {
  const char* bytes = (const char*)data;
  while (length > 0) {
    DWORD written = 0;
    if (!WriteFile(port->handle, bytes, (DWORD)length, &written, NULL) || written == 0) {
      remember_error("WriteFile");
      return -1;
    }
    bytes += written;
    length -= written;
  }
  return 0;
}

void serial_close(SerialPort* port) {
  if (port != NULL) {
    CloseHandle(port->handle);
    free(port);
  }
}

#else /* POSIX */

struct SerialPort {
  int fd;
};

static void remember_error(const char* what) {
  snprintf(g_error, sizeof(g_error), "%s: %s", what, strerror(errno));
}

static speed_t to_speed(uint32_t baudrate) {
  switch (baudrate) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return 0;
  }
}

SerialPort* serial_open(const char* name, uint32_t baudrate) {
  const speed_t speed = to_speed(baudrate);
  if (speed == 0) {
    snprintf(g_error, sizeof(g_error), "unsupported baudrate %u", (unsigned)baudrate);
    return NULL;
  }
  int fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    remember_error(name);
    return NULL;
  }
  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    remember_error("tcgetattr");
    close(fd);
    return NULL;
  }
  cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD;
  tio.c_cflag &= ~(tcflag_t)CRTSCTS;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    remember_error("tcsetattr");
    close(fd);
    return NULL;
  }
  tcflush(fd, TCIOFLUSH);

  SerialPort* port = calloc(1, sizeof(SerialPort));
  if (port == NULL) {
    close(fd);
    return NULL;
  }
  port->fd = fd;
  return port;
}

int serial_read(SerialPort* port, void* buffer, size_t size, int timeout_ms) {
  struct pollfd entry = {port->fd, POLLIN, 0};
  int ready = poll(&entry, 1, timeout_ms);
  if (ready < 0) {
    if (errno == EINTR) {
      return 0;
    }
    remember_error("poll");
    return -1;
  }
  if (ready == 0) {
    return 0;
  }
  if (entry.revents & (POLLERR | POLLNVAL)) {
    snprintf(g_error, sizeof(g_error), "serial port error");
    return -1;
  }
  ssize_t n = read(port->fd, buffer, size);
  if (n < 0) {
    if (errno == EAGAIN || errno == EINTR) {
      return 0;
    }
    remember_error("read");
    return -1;
  }
  if (n == 0) {
    snprintf(g_error, sizeof(g_error), "serial port closed");
    return -1; /* hang-up: device removed or pty closed */
  }
  return (int)n;
}

int serial_write_all(SerialPort* port, const void* data, size_t length) {
  const char* bytes = (const char*)data;
  while (length > 0) {
    ssize_t n = write(port->fd, bytes, length);
    if (n > 0) {
      bytes += n;
      length -= (size_t)n;
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      struct pollfd entry = {port->fd, POLLOUT, 0};
      if (poll(&entry, 1, 1000) <= 0) {
        snprintf(g_error, sizeof(g_error), "write timed out");
        return -1;
      }
      continue;
    }
    remember_error("write");
    return -1;
  }
  return 0;
}

void serial_close(SerialPort* port) {
  if (port != NULL) {
    close(port->fd);
    free(port);
  }
}

#endif
