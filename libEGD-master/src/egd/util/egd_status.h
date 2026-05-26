/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_UTIL_EGD_STATUS_H_
#define _LIBEGD_EGD_UTIL_EGD_STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief We will use this enum to check the status of all calls
 */
typedef enum {
  OK = 1,  /// Catch all for any success
  ERR_UNKNOWN = -1,  /// Catch all for any error that we do not know more about
  ERR_NULL = -2,  /// Occurs when a NULL value was passed, but NULL values are not allowed
  ERR_CURL_INIT = -3,  /// Occurs when we are unable to initialize curl
  ERR_CURL_REQUEST = -4,  /// Occurs when a curl request fails
  ERR_XML_PARSE = -5,  /// Occurs when we encounter an error parsing the XML into a string
  ERR_XML_PRODUCER_NOT_FOUND = -6,  /// Occurs when we cannot find the requested producer in the XML
  ERR_XML_EXCHANGE_NOT_FOUND = -7,  /// Occurs when we cannot find the requested exchange in the XML
  ERR_PAGE_NOT_FOUND = -8,  /// Occurs when we cannot find the requested page in the internal map
  ERR_VAR_NOT_FOUND = -9,  /// Occurs when we cannot find the requested variable in the internal map
  ERR_INVALID_ADDRESS = -10,  /// Occurs when an invalid address is passed
  ERR_INVALID_SOCKET = -11,  /// Occurs when an attempt to create a socket results in an invalid socket
  ERR_BIND = -12,  /// Occurs when we are unable to bind to an IP address
  ERR_BROADCAST = -13,  /// Occurs when we are unable to set broadcast on a socket
  ERR_UNABLE_TO_REUSE = -14,  /// Occurs when we are unable to set "reuse" state on a socket
  ERR_BUF_SIZE = -15,  /// Occurs when we are unable to set the buffer size on the socket
  ERR_MESSAGE_TOO_LARGE = -16,  /// Occurs when the message is too large (over 1400 bytes)
  ERR_PUBLISH = -17,  /// Occurs when we are unable to publish a message to the socket
  ERR_JSON_PARSE = -18,  /// Occurs when we are unable to parse JSON either in a publish or subscribe of the JSON client
  ERR_INVALID_JSON = -19,  /// Occurs when we were passed a non-object JSON object that we cannot parse in order to publish
  ERR_OUT_OF_BOUNDS = -20,  /// Occurs when we were asked to read or write beyond the limit of the EGD max payload size
  ERR_SIGNATURE_MISMATCH = -21,  /// Occurs when we attempted to update the config for a message, but the signature is different
} EgdStatus;  // TODO: This should really just be a typedef to an int

/**
 * @brief Converts an EGD status enum to the string representation
 * @param status The status to convert to a string
 * @return String representing the status code
 */
const char* egd_status_to_string(EgdStatus status);

#ifdef __cplusplus
}
#endif

#endif  // _LIBEGD_EGD_UTIL_EGD_STATUS_H_
