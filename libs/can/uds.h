#ifndef IOTWIN_UDS_H
#define IOTWIN_UDS_H

/**
 * @file uds.h
 * @brief Unified Diagnostic Services (ISO 14229-1) server on top of ISO-TP.
 *
 * Supported services (requests without the transport header):
 *
 *   10 ss        DiagnosticSessionControl   default (01) and extended (03) session
 *   11 rr        ECUReset                   hard (01) and soft (03) reset via callback
 *   14 gg gg gg  ClearDiagnosticInformation all DTCs (FFFFFF) or a single DTC
 *   19 01 mm     ReadDTCInformation         number of DTCs matching a status mask
 *   19 02 mm                                DTCs matching a status mask
 *   19 0A                                   all supported DTCs
 *   22 dd dd ..  ReadDataByIdentifier       up to UDS_MAX_DIDS_PER_REQUEST identifiers
 *   2E dd dd ..  WriteDataByIdentifier      extended session only
 *   3E 00        TesterPresent              keeps a non-default session alive
 *
 * The suppressPosRspMsgIndicationBit (0x80 in the sub-function) is honoured
 * for 10, 11 and 3E. Requests arrive physically (request_id, ISO-TP, up to
 * UDS_MAX_REQUEST bytes) or functionally (functional_id, single frames only);
 * for functional requests the NRCs 11, 12, 31, 7E and 7F are not sent.
 * Without a request for s3_ms the server falls back to the default session.
 *
 * Data identifiers and resets are handled by the application through
 * callbacks; DTCs live in an array owned by the application and are updated
 * with uds_dtc_report(). Portable C without heap use (host and firmware).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"
#include "isotp.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Service identifiers */
/**@{*/
#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL 0x10u
#define UDS_SID_ECU_RESET 0x11u
#define UDS_SID_CLEAR_DIAGNOSTIC_INFORMATION 0x14u
#define UDS_SID_READ_DTC_INFORMATION 0x19u
#define UDS_SID_READ_DATA_BY_IDENTIFIER 0x22u
#define UDS_SID_WRITE_DATA_BY_IDENTIFIER 0x2Eu
#define UDS_SID_TESTER_PRESENT 0x3Eu
#define UDS_SID_NEGATIVE_RESPONSE 0x7Fu
#define UDS_POSITIVE_RESPONSE_OFFSET 0x40u
#define UDS_SUPPRESS_POSITIVE_RESPONSE 0x80u
/**@}*/

/** @name Negative response codes */
/**@{*/
#define UDS_NRC_GENERAL_REJECT 0x10u
#define UDS_NRC_SERVICE_NOT_SUPPORTED 0x11u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED 0x12u
#define UDS_NRC_INCORRECT_LENGTH 0x13u
#define UDS_NRC_RESPONSE_TOO_LONG 0x14u
#define UDS_NRC_BUSY_REPEAT_REQUEST 0x21u
#define UDS_NRC_CONDITIONS_NOT_CORRECT 0x22u
#define UDS_NRC_REQUEST_OUT_OF_RANGE 0x31u
#define UDS_NRC_SECURITY_ACCESS_DENIED 0x33u
#define UDS_NRC_RESPONSE_PENDING 0x78u
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED_IN_SESSION 0x7Eu
#define UDS_NRC_SERVICE_NOT_SUPPORTED_IN_SESSION 0x7Fu
/**@}*/

/** @brief Diagnostic sessions. */
typedef enum {
  UDS_SESSION_DEFAULT = 0x01,
  UDS_SESSION_PROGRAMMING = 0x02,
  UDS_SESSION_EXTENDED = 0x03
} UdsSession;

/** @name ECUReset types */
/**@{*/
#define UDS_RESET_HARD 0x01u
#define UDS_RESET_SOFT 0x03u
/**@}*/

/** @name DTC status bits (ISO 14229-1 annex D) */
/**@{*/
#define UDS_DTC_TEST_FAILED 0x01u
#define UDS_DTC_TEST_FAILED_THIS_CYCLE 0x02u
#define UDS_DTC_PENDING 0x04u
#define UDS_DTC_CONFIRMED 0x08u
#define UDS_DTC_NOT_COMPLETED_SINCE_CLEAR 0x10u
#define UDS_DTC_FAILED_SINCE_CLEAR 0x20u
#define UDS_DTC_NOT_COMPLETED_THIS_CYCLE 0x40u
#define UDS_DTC_WARNING_INDICATOR 0x80u
/** Status bits this implementation maintains. */
#define UDS_DTC_STATUS_AVAILABILITY 0x7Fu
/** DTCFormatIdentifier ISO_14229-1_DTCFormat. */
#define UDS_DTC_FORMAT_ISO14229 0x01u
/**@}*/

/** @name Standard data identifiers */
/**@{*/
#define UDS_DID_ACTIVE_SESSION 0xF186u
#define UDS_DID_SPARE_PART_NUMBER 0xF187u
#define UDS_DID_SOFTWARE_VERSION 0xF189u
#define UDS_DID_SERIAL_NUMBER 0xF18Cu
#define UDS_DID_VIN 0xF190u
/**@}*/

/** @brief Functional (broadcast) request id of IoTwin, as in OBD. */
#define UDS_FUNCTIONAL_ID 0x7DFu

#define UDS_MAX_REQUEST 256u
#define UDS_MAX_RESPONSE 512u
#define UDS_MAX_DIDS_PER_REQUEST 8u
#define UDS_DEFAULT_P2_MS 50u
#define UDS_DEFAULT_P2_STAR_MS 5000u
#define UDS_DEFAULT_S3_MS 5000u

/** @brief One diagnostic trouble code: 24-bit code and status byte. */
typedef struct {
  uint32_t code;
  uint8_t status;
} UdsDtc;

/** @brief Application callbacks. All are optional. */
typedef struct {
  /**
   * @brief Reads a data identifier into @p out.
   * @return Number of bytes written, or the negated NRC (e.g. -UDS_NRC_REQUEST_OUT_OF_RANGE
   *         for an unknown identifier, -UDS_NRC_RESPONSE_TOO_LONG if @p capacity is too small).
   */
  int (*read_did)(void* context, uint16_t did, uint8_t* out, size_t capacity);
  /**
   * @brief Writes a data identifier (only called in the extended session).
   * @return 0 on success or the NRC. NULL = WriteDataByIdentifier not supported.
   */
  uint8_t (*write_did)(void* context, uint16_t did, const uint8_t* data, size_t length);
  /** @brief ECUReset, called once the positive response has been transmitted. */
  void (*reset)(void* context, uint8_t reset_type);
  /** @brief The active session changed (request or S3 timeout). */
  void (*session_changed)(void* context, UdsSession session);
  void* context;
} UdsServerHandlers;

/** @brief Server configuration. */
typedef struct {
  uint32_t request_id;    /**< Physical request id (tester -> server). */
  uint32_t response_id;   /**< Response id (server -> tester). */
  uint32_t functional_id; /**< Functional request id, 0 = not used. */
  bool extended_id;
  uint16_t p2_ms;         /**< Reported in the session response. */
  uint16_t p2_star_ms;    /**< Reported in the session response (10 ms resolution). */
  uint32_t s3_ms;         /**< Session timeout without requests. */
  bool (*send)(void* context, const CanFrame* frame); /**< CAN driver, false = queue full. */
  void* send_context;
  UdsServerHandlers handlers;
  UdsDtc* dtcs;           /**< Application owned DTC table (may be NULL). */
  size_t dtc_count;
} UdsServerConfig;

/** @brief Server state. */
typedef struct {
  UdsServerConfig config;
  IsoTpLink link;
  uint8_t request[UDS_MAX_REQUEST];
  uint8_t response[UDS_MAX_RESPONSE];
  UdsSession session;
  uint64_t last_request_us;
  bool reset_pending;
  uint8_t reset_type;
  uint32_t requests;           /**< Requests processed. */
  uint32_t negative_responses; /**< Negative responses sent. */
} UdsServer;

/** @brief Defaults: functional id 0x7DF, P2 50 ms, P2* 5 s, S3 5 s, standard ids. */
void uds_server_default_config(UdsServerConfig* config, uint32_t request_id, uint32_t response_id);

void uds_server_init(UdsServer* server, const UdsServerConfig* config);

/**
 * @brief Hands a received CAN frame to the server.
 * @return true if the frame was addressed to the server (physical or functional).
 */
bool uds_server_on_frame(UdsServer* server, const CanFrame* frame, uint64_t now_us);

/** @brief Drives ISO-TP, executes pending resets and checks the S3 timeout. */
void uds_server_poll(UdsServer* server, uint64_t now_us);

/**
 * @brief Processes one request without transport (used internally, public for tests).
 * @return Length of the response in @p response, 0 = no response (suppressed).
 */
size_t uds_server_process(UdsServer* server, const uint8_t* request, size_t length, bool functional,
                          uint8_t* response, size_t capacity, uint64_t now_us);

UdsSession uds_server_session(const UdsServer* server);

/** @brief Initial DTC: test not completed since clear and this cycle (status 0x50). */
void uds_dtc_init(UdsDtc* dtc, uint32_t code);

/** @brief Result of a monitor: updates the status bits for a failed or passed test. */
void uds_dtc_report(UdsDtc* dtc, bool failed);

/** @brief New operation cycle (e.g. after a reset): clears the "this cycle" results. */
void uds_dtc_start_operation_cycle(UdsDtc* dtc);

/** @brief DTC of the server's table with the given code, NULL if unknown. */
UdsDtc* uds_server_find_dtc(UdsServer* server, uint32_t code);

/** @name Text helpers (testers, logging) */
/**@{*/
const char* uds_service_name(uint8_t sid);
const char* uds_nrc_name(uint8_t nrc);
const char* uds_session_name(uint8_t session);
/** @brief Formats a DTC like "B1001-11"; @p capacity must be at least 9. */
void uds_format_dtc(uint32_t code, char* out, size_t capacity);
/** @brief Parses "B1001-11", "B1001" (failure type 00) or 6 hex digits ("900111"). */
bool uds_parse_dtc(const char* text, uint32_t* code);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_UDS_H */
