#ifndef IOTWIN_PAL_RT_H
#define IOTWIN_PAL_RT_H

/**
 * @file pal_rt.h
 * @brief Real-time setup of the calling thread.
 *
 * Linux (reference platform, ideally with a PREEMPT_RT kernel):
 *  - SCHED_FIFO with the requested priority (needs CAP_SYS_NICE or an rtprio limit)
 *  - CPU pinning via pthread_setaffinity_np
 *  - mlockall() to avoid page faults (needs CAP_IPC_LOCK or a memlock limit)
 *
 * Windows: best effort only. Thread priority and affinity are raised/set,
 * memory locking is not supported and there are no timing guarantees.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Requested real-time settings. */
typedef struct {
  int priority;     /**< SCHED_FIFO priority 1..99, 0 = keep the default scheduler. */
  int cpu;          /**< CPU core to pin the thread to, -1 = no pinning. */
  bool lock_memory; /**< Lock current and future memory pages into RAM. */
} PalRtConfig;

/** @brief Fills @p config with "no real-time settings requested". */
void pal_rt_default_config(PalRtConfig* config);

/** @brief Returns true if @p config requests any real-time setting. */
bool pal_rt_requested(const PalRtConfig* config);

/**
 * @brief Applies @p config to the calling thread.
 * @param message Receives a human readable summary (applied settings or errors).
 * @return true if every requested setting was applied.
 */
bool pal_rt_apply(const PalRtConfig* config, char* message, size_t message_size);

/** @brief True if the running kernel reports PREEMPT_RT (always false on Windows). */
bool pal_rt_kernel_is_preempt_rt(void);

/** @brief Name of the platform the binary was built for ("Linux", "Windows", ...). */
const char* pal_platform_name(void);

/**
 * @brief Applies @p config (if anything is requested) and logs the outcome
 *        together with the platform / kernel real-time capabilities.
 */
void pal_rt_setup_and_log(const PalRtConfig* config);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_PAL_RT_H */
