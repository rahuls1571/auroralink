/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CLIENT_JSON_CLIENT_HPP_
#define _LIBEGD_EGD_CLIENT_JSON_CLIENT_HPP_

#include <netdb.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <thread>
#include <memory>
#include "egd/client/json_client.h"
#include "egd/client/egd_client.hpp"

namespace egd {

namespace client {

/**
 * @brief Struct that will wrap our custom user data, and the user's data to be passed into the parent client
 */
struct JsonUserData {
  EgdJsonSubCallback callback;  /// The callback that we will call from our custom callback, this should be provided by the user
  void* user_data;  /// The user's user data that we will pass to their callback

  /**
   * @brief Constructor that will allow us to emplace this struct into a vector
   * @param callback_ The callback to use in this struct
   * @param user_data_ The user_data to use in this struct
   */
  JsonUserData(EgdJsonSubCallback callback_, void* user_data_) : callback(callback_), user_data(user_data_) {}
};

/**
 * @brief C++ class that will implement the required functionality to communicate on EGD
 */
class JsonClient : protected EgdClient {
 protected:
  std::vector<std::shared_ptr<JsonUserData>> json_user_data_;  /// This vector will keep track of the user data that we pass into the parent client so it does not go out of scope

  /**
   * @brief Proxy callback that will be sent to the parent client so we can parse the payload before handing it back to the user
   * @param page_info Information on the page requested for this subscription
   * @param header The header of the message that was just received
   * @param data The data section of the message that you want to publish (not including the header)
   * @param data_len The length of the data in bytes (must be <= 1400)
   * @param user_data User data passed in at time of subscribe, for us this will contain the user's callback, and their user data
   */
  static void JsonProxyCallback(const EgdPageInfo* page_info, const DataProductionHdr* header, const EgdStatus status,
      const uint8_t* data, size_t data_len, void* user_data);

 public:
  /**
   * @brief Constructs the client
   */
  explicit JsonClient() = default;

  /**
   * @brief Publishes the given message to the page passed in
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param page_info Information on the page to publish to, preferably obtained from an EgdConfig instance
   * @param message The message to publish to the page (in JSON)
   * @return EgdStatus reflecting the status of the publish call
   */
  EgdStatus Publish(const std::string& host, uint16_t port, const EgdPageInfo& page_info,
      const std::string& message);

  /**
   * @brief Subscribes to a specific EGD page and calls the provided callback whenever the page is received
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param page_info Information on the page, preferably obtained from an EgdConfig instance
   * @param callback The callback to call when a page is received
   * @param user_data Arbitrary data to pass into the callback on every call
   * @return EgdStatus reflecting the status of the subscribe call
   */
  EgdStatus Subscribe(const std::string& host, uint16_t port, const EgdPageInfo& page_info, EgdJsonSubCallback callback,
      void* user_data);
  EgdStatus SubscribeBlocking(const std::string& host, uint16_t port, const EgdPageInfo& page_info, EgdJsonSubCallback callback,
      void* user_data);
};

}  // namespace client

}  // namespace egd

#endif  // _LIBEGD_EGD_CLIENT_JSON_CLIENT_HPP_
