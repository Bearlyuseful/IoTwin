#ifndef CAN_BUS_H
#define CAN_BUS_H

/**
 * @file can_bus.h
 * @brief Thread-safe FIFO of CAN frames: the local view of a node on the bus.
 *
 * A node uses one CanBus as mailbox, e.g. frames received from the network
 * are queued here until the control loop processes them, or frames typed
 * on the console wait here until the control loop sends them. Each instance
 * has its own storage and lock, so a process can have several of them.
 */

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_BUS_BUFFER_SIZE 32 /**< Capacity of the FIFO in frames. */

/** @brief Ring buffer state. Use only through the functions below. */
typedef struct {
  CanFrame buffer[CAN_BUS_BUFFER_SIZE];
  size_t head;      /**< Index of the oldest frame. */
  size_t tail;      /**< Index of the next free slot. */
  size_t count;     /**< Number of queued frames. */
  uint64_t dropped; /**< Frames rejected because the FIFO was full. */
  pthread_mutex_t lock;
} CanBus;

/** @brief Initialises an empty FIFO. */
bool can_bus_init(CanBus* bus);

/** @brief Appends a copy of @p frame. Returns false if the FIFO is full or the frame invalid. */
bool can_bus_send(CanBus* bus, const CanFrame* frame);

/** @brief Removes the oldest frame into @p frame. Returns false if the FIFO is empty. */
bool can_bus_receive(CanBus* bus, CanFrame* frame);

/** @brief Number of queued frames. */
size_t can_bus_count(CanBus* bus);

/** @brief Number of frames rejected because the FIFO was full. */
uint64_t can_bus_dropped(CanBus* bus);

/** @brief Releases the lock; the FIFO must not be used afterwards. */
void can_bus_destroy(CanBus* bus);

#ifdef __cplusplus
}
#endif

#endif /* CAN_BUS_H */
