/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */

/**
 * @file egd_spec.h
 * @brief Definitions relating to the EGD protocol.
 *        This file is used internally to standardize the usage
 *        of the EGD protocol across platforms
 */
#ifndef _LIBEGD_EGD_H_
#define _LIBEGD_EGD_H_

#include <stdint.h>

/**
 * @def SOCKET_TYPE
 * @brief <int> Sets the socket type.
 */
#define SOCKET_TYPE int

/**
 * @def SOCKET_INVALID
 * @brief <int> Registers a empty socket.
 */
#define SOCKET_INVALID 0

/**
 * @def SOCKET_ERROR
 * @brief <int> Registers a socket error.
 */
#define SOCKET_ERROR -1

/**
 * @def DATA_PORT
 * @brief <int> Constant. Port for EGD data messages.
 */
#define DATA_PORT 18246

/**
 * @def COMMAND_PORT
 * @brief <int> Constant. Port for EGD command messages.
 */
#define COMMAND_PORT 7937

/**
 * @def TRANSPORT_SUCCESS
 * @brief <int> Registers a transport success.
 */
#define TRANSPORT_SUCCESS 1

/**
 * @def TRANSPORT_FAILURE
 * @brief <int> Registers a transport failure.
 */
#define TRANSPORT_FAILURE 0

/**
 * @def EGD_MESSAGE_VALID
 * @brief <int> Registers a valid EGD message.
 */
#define EGD_MESSAGE_VALID 1

/**
 * @def EGD_MESSAGE_ERR
 * @brief <int> Registers a EGD message error.
 */
#define EGD_MESSAGE_ERR -1

/**
 * @def EGD_MESSAGE_USELESS
 * @brief <int> Registers a message not to be acted on.
 */
#define EGD_MESSAGE_USELESS -2

/**
 * @def EGD_MESSAGE_UNHEALTHY
 * @brief <int> Registers a message as one which forces a consumer into an unhealthy state
 *
 * See EGD spec Section 2.3.2 (Consumed Exchange Status) Table 5
 */
#define EGD_MESSAGE_UNHEALTHY -3

/**
 * @def EGD_MESSAGE_SIG_ERROR
 * @brief <int> Registers a message as one which forces all consumers with the same
 *        producer + exchange ID into an unhealthy state
 *
 * See EGD spec Section 2.3.2 (Consumed Exchange Status) Table 5
 */
#define EGD_MESSAGE_SIG_ERROR -4

/**
 * @def TRANSPORT_NO_MESSAGE
 * @brief <int> There is no message on the socket
 */
#define TRANSPORT_NO_MESSAGE 2

/**
 * @def MESSAGE_FAILURE
 * @brief <char*> Registers a transport failure.
*/
#define MESSAGE_FAILURE "EGD_DATA_FAILURE"

/**
 * @def BYTE
 * @brief <uint8_t> Variable size constant for EGD data header.
 */
#define BYTE uint8_t

/**
 * @def WORD
 * @brief <uint16_t> Variable size constant for EGD data header.
 */
#define WORD uint16_t

/**
 * @def DWORD
 * @brief <uint32_t> Variable size constant for EGD data header.
 */
#define DWORD uint32_t

/**
 * @def MAX_EGD_PAYLOAD
 * @brief Maximum size of an EGD payload
 */
#define MAX_EGD_PAYLOAD 1400

/**
 * @struct EGD_Time
 * @brief This is a time struct similar to timeval.
 *        Struct is used in EGD data header.
 */
struct DataTime {
   uint32_t tv_sec;  /// Time in seconds
   int32_t  tv_nsec;  /// Time in nano seconds
};

/**
 * @struct DataProductionHdr
 * @brief This struct forms the EGD data header.
 */
struct DataProductionHdr {
  BYTE PDUType;  /// PDUType <uint8_t> Specifies the type of message. Data: [13].
  BYTE Version;  /// Specifies the version. Always [1] for compatibility.
  WORD ReqID;  /// Specifies the number of messages received.
  DWORD ProducerID;  /// Identifies the producer EGD node making data.
  DWORD ExchangeID;  /// Identifies the exchange on a specific EGD node.
  struct DataTime Timestamp;  /// Specifies the Timestamp. EGD is time-synchronized.
  WORD Status;  /// Specifies the status of the data.
  WORD ReservedA;  /// Should be empty. Reserved.
  WORD Signature;  /// Specifies the configuration.
  WORD ReservedB;  /// Should be empty. Reserved.
  DWORD ConfigTimeSecs;  /// Time that this page was configured
};

/**
 * @struct DataProductionMessage
 * @brief This struct forms the EGD data message. Message is set at max length.
 */
struct DataProductionMessage {
  struct DataProductionHdr Hdr;  /// Holds the EGD header
  BYTE Data[MAX_EGD_PAYLOAD];  /// Holds the EGD data
};

#endif  // _LIBEGD_EGD_H_
