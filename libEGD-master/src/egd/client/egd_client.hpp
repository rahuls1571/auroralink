/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CLIENT_EGD_CLIENT_HPP_
#define _LIBEGD_EGD_CLIENT_EGD_CLIENT_HPP_

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
#include "egd/client/egd_client.h"

namespace egd {

namespace client {

/**
 * @brief this struct will hold information that both publisher and subscribers need
 */
struct EgdConnectionInfo {
  std::string host;  /// The host that will be connected to
  uint16_t port;  /// The port that will be connected to
  sockaddr_in addr;  /// The address object that will be used to connect to the socket
  SOCKET_TYPE socket;  /// The socket that will be used to send or receive data
};

/**
 * @brief This struct will hold information on each publisher so we can keep state
 */
struct EgdPubInfo : public EgdConnectionInfo {
  int request_id = 0;  /// The request ID for this publisher
};

/**
 * @brief This struct will hold information on the subscription callback, and supporting information
 */
struct EgdSubCallbackInfo {
  EgdSubCallback callback;  /// The callback to call when a message is received
  EgdPageInfo page_info;  /// The page info about the requested page
  void* user_data;  /// Arbitrary user data passed in by the user

  /**
   * @brief Constructor that will initialize all member variables
   * @param callback_ The callback to initialize
   * @param page_info_ The page info to initialize
   * @param user_data_ The user data to initialize
   */
  EgdSubCallbackInfo(EgdSubCallback callback_, EgdPageInfo page_info_, void* user_data_)
      : callback(callback_), page_info(page_info_), user_data(user_data_) {}
};

/**
 * @brief This struct will hold information on each subscriber so we can keep state
 */
struct EgdSubInfo : public EgdConnectionInfo {
  std::shared_ptr<std::thread> p_thread;  /// The thread that will handle polling the socket
  std::vector<EgdSubCallbackInfo> callbacks;  /// The callbacks that will be called when data is received on this subscription
  pthread_rwlock_t callbacks_lock;  /// We will use this lock to lock the callbacks vector (this is a pthread lock because C++11 does not have a read/write lock implementation)

  /**
   * @brief Initializes the lock since it is a C style object that needs to be initialized
   */
  EgdSubInfo();

  /**
   * @brief destroys the lock since it has to be cleaned up
   */
  ~EgdSubInfo();
};

/**
 * @brief This disgusting tuple will be used as the key in our map to lookup information about
 *        the publisher we have cached
 */
using EgdPubKey = std::tuple<std::string, uint16_t, uint32_t, uint32_t>;

/**
 * @brief This disgusting tuple will be used as the key in our map to lookup information about
 *        the subscriber we have cached
 */
using EgdSubKey = std::tuple<std::string, uint16_t>;

/**
 * @brief C++ class that will implement the required functionality to communicate on EGD
 */
class EgdClient {
 protected:
  std::map<EgdPubKey, EgdPubInfo> publisher_map_;  /// Cached information on the publishers
  std::map<EgdSubKey, EgdSubInfo> subscriber_map_;  /// Cached information on the subscribers
  std::atomic<bool> run_flag_{true};  /// This will be set to false when in the destructor

  /**
   * @brief Converts a string (either hostname or ip) to an integer representation of an IP
   * @param host The string to convert to an IP
   * @return Integer representation of the IP address
   */
  static long GetIpFromString(const std::string& host);

  /**
   * @brief Gets an address object based on a host and port
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @return EgdStatus reflecting the status of the publish call
   */
  static EgdStatus GetAddr(const std::string& host, const uint16_t port, sockaddr_in* addr);

  /**
   * @brief Connects a publisher and stores the information in the info object
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param pub_info Publisher info to get information from and to store the connected socket
   * @return EgdStatus reflecting the status of the publish call
   */
  static EgdStatus PublishConnect(const std::string& host, uint16_t port, EgdPubInfo* pub_info);

  /**
   * @brief Connects a subscriber and stores the information in the info object
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param sub_info Subscriber info to get information from and to store the connected socket
   * @return EgdStatus reflecting the status of the publish call
   */
  static EgdStatus SubscribeConnect(const std::string& host, uint16_t port, EgdSubInfo* sub_info);

  /**
   * @brief Function that will be run in a thread to constantly poll a socket for messages
   * @param sub_key The key that will be used to look up information on this subscription
   */
  void PollSocket(EgdSubInfo* sub_key);
 public:
  /**
   * @brief Constructs the client
   */
  explicit EgdClient() = default;

  /**
   * @brief Deletes the client
   */
  ~EgdClient();

  /**
   * @brief Publishes the given message to the page passed in
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param page_info Information on the page to publish to, preferably obtained from an EgdConfig instance
   * @param data The data section of the message that you want to publish (not including the header)
   * @param data_len The length of the data in bytes (must be <= 1400)
   * @return EgdStatus reflecting the status of the publish call
   */
  EgdStatus Publish(const std::string& host, uint16_t port, const EgdPageInfo& page_info,
      const uint8_t* data, size_t data_len);

  /**
   * @brief Subscribes to a specific EGD page and calls the provided callback whenever the page is received
   * @param host The host to use as the IP of the address
   * @param port The port to use in the address
   * @param page_info Information on the page, preferably obtained from an EgdConfig instance
   * @param callback The callback to call when a page is received
   * @param user_data Arbitrary data to pass into the callback on every call
   * @return EgdStatus reflecting the status of the subscribe call
   */
  EgdStatus Subscribe(const std::string& host, uint16_t port, const EgdPageInfo& page_info, EgdSubCallback callback,
      void* user_data);
  EgdStatus SubscribeBlocking(const std::string& host, uint16_t port, const EgdPageInfo& page_info, EgdSubCallback callback,
      void* user_data);
};

}  // namespace client

}  // namespace egd

#endif  // _LIBEGD_EGD_CLIENT_EGD_CLIENT_HPP_
