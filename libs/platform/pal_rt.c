#if defined(__linux__)
#define _GNU_SOURCE
#endif

#include "pal_rt.h"

#include <stdio.h>
#include <string.h>

#include "log.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#endif

void pal_rt_default_config(PalRtConfig* config) {
  config->priority = 0;
  config->cpu = -1;
  config->lock_memory = false;
}

bool pal_rt_requested(const PalRtConfig* config) {
  return config->priority > 0 || config->cpu >= 0 || config->lock_memory;
}

/* Appends "text" to the message buffer, separated by "; ". */
static void append(char* message, size_t size, const char* text) {
  if (message == NULL || size == 0) {
    return;
  }
  size_t used = strlen(message);
  if (used > 0 && used + 2 < size) {
    strcat(message, "; ");
    used += 2;
  }
  if (used < size) {
    snprintf(message + used, size - used, "%s", text);
  }
}

#if defined(__linux__)

bool pal_rt_apply(const PalRtConfig* config, char* message, size_t message_size) {
  bool ok = true;
  char part[160];
  if (message && message_size) {
    message[0] = '\0';
  }

  if (config->lock_memory) {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
      append(message, message_size, "memory locked");
    } else {
      snprintf(part, sizeof(part), "mlockall failed: %s", strerror(errno));
      append(message, message_size, part);
      ok = false;
    }
  }

  if (config->cpu >= 0) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(config->cpu, &set);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc == 0) {
      snprintf(part, sizeof(part), "pinned to CPU %d", config->cpu);
    } else {
      snprintf(part, sizeof(part), "CPU pinning failed: %s", strerror(rc));
      ok = false;
    }
    append(message, message_size, part);
  }

  if (config->priority > 0) {
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = config->priority;
    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc == 0) {
      snprintf(part, sizeof(part), "SCHED_FIFO priority %d", config->priority);
    } else {
      snprintf(part, sizeof(part), "SCHED_FIFO priority %d failed: %s", config->priority,
               strerror(rc));
      ok = false;
    }
    append(message, message_size, part);
  }
  return ok;
}

bool pal_rt_kernel_is_preempt_rt(void) {
  FILE* file = fopen("/sys/kernel/realtime", "r");
  if (file != NULL) {
    int value = 0;
    int read = fscanf(file, "%d", &value);
    fclose(file);
    if (read == 1 && value == 1) {
      return true;
    }
  }
  struct utsname name;
  if (uname(&name) == 0 && strstr(name.version, "PREEMPT_RT") != NULL) {
    return true;
  }
  return false;
}

const char* pal_platform_name(void) { return "Linux"; }

#elif defined(_WIN32)

bool pal_rt_apply(const PalRtConfig* config, char* message, size_t message_size) {
  bool ok = true;
  char part[160];
  if (message && message_size) {
    message[0] = '\0';
  }
  if (config->lock_memory) {
    append(message, message_size, "memory locking not supported on Windows");
    ok = false;
  }
  if (config->cpu >= 0) {
    if (config->cpu < 64 &&
        SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << config->cpu) != 0) {
      snprintf(part, sizeof(part), "pinned to CPU %d", config->cpu);
    } else {
      snprintf(part, sizeof(part), "CPU pinning to %d failed", config->cpu);
      ok = false;
    }
    append(message, message_size, part);
  }
  if (config->priority > 0) {
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
      append(message, message_size, "thread priority TIME_CRITICAL (best effort)");
    } else {
      append(message, message_size, "raising thread priority failed");
      ok = false;
    }
  }
  return ok;
}

bool pal_rt_kernel_is_preempt_rt(void) { return false; }

const char* pal_platform_name(void) { return "Windows"; }

#else

bool pal_rt_apply(const PalRtConfig* config, char* message, size_t message_size) {
  (void)config;
  if (message && message_size) {
    snprintf(message, message_size, "real-time setup not supported on this platform");
  }
  return false;
}

bool pal_rt_kernel_is_preempt_rt(void) { return false; }

const char* pal_platform_name(void) { return "unknown"; }

#endif

void pal_rt_setup_and_log(const PalRtConfig* config) {
  const bool preempt_rt = pal_rt_kernel_is_preempt_rt();
  log_info("Platform: %s, PREEMPT_RT kernel: %s", pal_platform_name(), preempt_rt ? "yes" : "no");
#if defined(_WIN32)
  log_info("Windows build runs best effort - timing results are not real-time representative");
#endif
  if (config == NULL || !pal_rt_requested(config)) {
    return;
  }
  char message[256];
  if (pal_rt_apply(config, message, sizeof(message))) {
    log_info("Real-time setup: %s", message);
  } else {
    log_warn("Real-time setup incomplete: %s", message);
  }
  if (!preempt_rt) {
    log_warn("Kernel is not PREEMPT_RT - expect higher latency/jitter than on a real-time system");
  }
}
