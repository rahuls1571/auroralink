/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#include "egd/util/egd_status.h"

/*****************************
 * Begin C Definitions *
 ****************************/

extern "C" {

const char* egd_status_to_string(const EgdStatus status) {
  switch (status) {
    case OK:
      return "Ok";
    case ERR_UNKNOWN:
      return "Unknown error";
    case ERR_NULL:
      return "Null value passed to library";
    case ERR_CURL_INIT:
      return "Unable to initialize curl";
    case ERR_CURL_REQUEST:
      return "Unable to make curl request";
    case ERR_XML_PARSE:
      return "Unable to parse XML document into string";
    case ERR_XML_PRODUCER_NOT_FOUND:
      return "Unable to find the producer section of the XML";
    case ERR_XML_EXCHANGE_NOT_FOUND:
      return "Unable to find the exchange section of the XML";
    case ERR_PAGE_NOT_FOUND:
      return "Unable to find requested page";
    case ERR_VAR_NOT_FOUND:
      return "Unable to find requested variable";
    case ERR_INVALID_ADDRESS:
      return "Invalid address";
    case ERR_INVALID_SOCKET:
      return "Invalid socket, it is possible that all FD's are occupied";
    case ERR_BIND:
      return "Unable to bind to requested IP address";
    case ERR_BROADCAST:
      return "Unable to set broadcast on socket";
    case ERR_UNABLE_TO_REUSE:
      return "Unable to set reuse on socket";
    case ERR_BUF_SIZE:
      return "Unable to set the buffer size on the socket";
    case ERR_MESSAGE_TOO_LARGE:
      return "Message too large";
    case ERR_PUBLISH:
      return "Unable to publish message";
    case ERR_JSON_PARSE:
      return "Unable to parse JSON string";
    case ERR_INVALID_JSON:
      return "Invalid JSON passed to publish function";
    case ERR_OUT_OF_BOUNDS:
      return "Attempted to read/write outside bounds of EGD payload";
    default:
      return "Unknown error";
  }
}

}
