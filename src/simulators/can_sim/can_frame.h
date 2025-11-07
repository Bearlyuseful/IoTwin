#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include <stdint.h>

/** 
 * @file can_frame.h 
 * @brief Defines the data structures for standard and extended CAN frames.
 *
 * This header contains bit-accurate representations of standard(11-bit ID)
 * and extended (29-bit ID) CAN frames as defined by ISO 11898-1.
 */

/**
 * @struct CanStandardFrame
 * @brief Represents a standard CAN frame (11-bit Identifier)
 */

typedef struct{
  uint8_t start_of_frame    : 1; /**< Start of frame (dominant bit, always 0). */
  uint16_t identifier       : 11; /**< 11-bit message identifier. */
  uint8_t rtr               : 1; /**< Remote Transmission Request flag. */
  uint8_t ide               : 1; /**< Identifier Extension bit (0 = Standard frame). */
  uint8_t dlc               : 4; /**< Data Length Code (0-8). */
  uint8_t data[8]; /**< Data field (0-8 bytes). */
  uint16_t crc              : 15; /**< CRC sequence (15 bits). */
  uint8_t crc_delimiter     : 1; /**< CRC delimiter (recessive bit). */
  uint8_t ack_slot          : 1; /**< ACK slot bit (dominant = received OK). */
  uint8_t ack_delimiter     : 1; /**< ACK delimiter bit (recessive). */
  uint8_t eof               : 7; /** End of frame (all recessive bits). */
} CanStandardFrame;

/**
 * @struct CanExtendedFrame
 * @brief Represents an extended CAN Frame (29-bit Identifier)
 */

typedef struct {
  uint8_t start_of_frame    : 1; /**< Start of frame (dominant bit, always 0). */
  uint16_t identifier_base  : 11; /**< Base identifier (first 11 bits). */
  uint8_t srr               : 1; /**< Substitute Remote Request bit (recessive). */
  uint32_t identifier_ext   : 18; /**< Extended identifier (additional 18 bits). */
  uint8_t rtr               : 1; /**< Remote Transmission Request flag. */
  uint8_t r1                : 1; /**< Reserved bit. */
  uint8_t r0                : 1; /**< Reserved bit. */
  uint8_t dlc               : 4; /**< Data Length Code (0–8). */
  uint8_t data[8]; /**< Data field (0–8 bytes). */
  uint16_t crc              : 15; /**< CRC sequence (15 bits). */
  uint8_t crc_delimiter     : 1; /**< CRC delimiter (recessive). */
  uint8_t ack_slot          : 1; /**< ACK slot bit. */
  uint8_t ack_delimiter     : 1; /**< ACK delimiter. */
  uint8_t eof               : 7; /**< End of frame (all recessive bits). */
} CanExtendedFrame;

/** 
 * @enum CanFrameType
 * @brief Specifies whether the CAN frame is standard or extended.
 */

typedef enum {
  CAN_FRAME_STANDARD, /**< 11-bit identifier frame. */
  CAN_FRAME_EXTENDED  /**< 29-bit identifier frame. */
} CanFrameType;

/** 
 * @struct CanFrame
 * @brief Generic CAN frame container 
 *
 * Allows access to both standard and extended frames via a common union.
 */

typedef struct {
  CanFrameType type;  /**< Indicates the frame type (standard or extended). */
  union {
    CanStandardFrame std; /**< Standard CAN frame (11-bit). */
    CanExtendedFrame ext; /**< Extended CAN frame (29-bit). */
  } frame;
} CanFrame;



#endif // !CAN_FRAME_H
