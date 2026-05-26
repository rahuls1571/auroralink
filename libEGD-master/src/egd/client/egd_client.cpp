/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <mutex>
#include <cstring>
#include <iostream>
#include <errno.h>
#include "egd/config/egd_config.hpp"
#include "egd/client/egd_client.hpp"

namespace egd {

namespace client {

using ::egd::config::EgdConfig;

static constexpr auto PDU_TYPE = 13;
static constexpr auto PAYLOAD_VERSION = 1;
static constexpr auto INVALID_SOCKET = 0;
static constexpr auto SHOULD_BROADCAST = 1;
static constexpr auto SHOULD_REUSE_ADDR = 1;
static constexpr auto BUF_SIZE = 128 * 1024;
static constexpr auto POLL_TIMEOUT_MS = 1000;

EgdSubInfo::EgdSubInfo() {
  pthread_rwlock_init(&callbacks_lock, nullptr);
}

EgdSubInfo::~EgdSubInfo() {
  pthread_rwlock_destroy(&callbacks_lock);
}

long EgdClient::GetIpFromString(const std::string& host) {
  const auto ip = inet_addr(host.c_str());
  if (ip != INADDR_NONE) {
    return ip;
  }

  const auto host_info = gethostbyname(host.c_str());
  if (host_info != nullptr) {
    return *reinterpret_cast<long*>(host_info->h_addr_list[0]);
  }

  return -1;
}

EgdStatus EgdClient::GetAddr(const std::string& host, const uint16_t port, sockaddr_in* addr) {
  // We may want to remove this, but for now reset all the bytes in the address
  memset(reinterpret_cast<void*>(addr), 0, sizeof(*addr));

  // Get the long representation of the IP address
  const long publish_ip = EgdClient::GetIpFromString(host);
  if (publish_ip < 0) {
    return EgdStatus::ERR_INVALID_ADDRESS;
  }

  // Finally set up the address
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = publish_ip;
  addr->sin_port = reinterpret_cast<uint16_t>(htons(port));
  return EgdStatus::OK;
}

EgdStatus EgdClient::PublishConnect(const std::string& host, const uint16_t port, EgdPubInfo* pub_info) {
  // Save the host and port into the info object
  pub_info->host = host;
  pub_info->port = port;

  // Get the address for the socket
  EgdStatus status;
  if ((status = EgdClient::GetAddr(pub_info->host, pub_info->port, &pub_info->addr)) != EgdStatus::OK) {
    return status;
  }

  // Set up the publish socket
  if ((pub_info->socket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
    return EgdStatus::ERR_INVALID_SOCKET;
  }

  // Set the publish socket to broadcast
  int should_broadcast = SHOULD_BROADCAST;
  if (setsockopt(pub_info->socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&should_broadcast), sizeof(should_broadcast))) {
    return EgdStatus::ERR_BROADCAST;
  }

  return EgdStatus::OK;
}

EgdStatus EgdClient::SubscribeConnect(const std::string& host, const uint16_t port, EgdSubInfo* sub_info) {
  // Save the host and port into the info object
  sub_info->host = host;
  sub_info->port = port;

  // Get the address for the socket
  EgdStatus status;
  if ((status = EgdClient::GetAddr(sub_info->host, sub_info->port, &sub_info->addr)) != EgdStatus::OK) {
    return status;
  }

  // Create the subscribe socket
  if ((sub_info->socket = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
    return EgdStatus::ERR_INVALID_SOCKET;
  }

  // Allow other applications to bind on this socket
  int should_reuse_addr = SHOULD_REUSE_ADDR;
  if (setsockopt(sub_info->socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&should_reuse_addr), sizeof(should_reuse_addr))) {
    return EgdStatus::ERR_UNABLE_TO_REUSE;
  }

  // Bind on the subscribe socket
  if (bind(sub_info->socket, reinterpret_cast<sockaddr*>(&sub_info->addr), sizeof(sub_info->addr)) == SOCKET_ERROR) {
    return EgdStatus::ERR_BIND;
  }

  // Set the socket buf size
  int buf_size = BUF_SIZE;
  if (setsockopt(sub_info->socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&buf_size), sizeof(buf_size))) {
    return EgdStatus::ERR_BUF_SIZE;
  }

  return EgdStatus::OK;
}

EgdClient::~EgdClient() {
  // Set the run flag to false, and exit all the threads and close the sockets
  run_flag_ = false;
  for (const auto& sub_entry : subscriber_map_) {
    sub_entry.second.p_thread->join();
    close(sub_entry.second.socket);
  }

  // Close all of the publisher sockets
  for (const auto& pub_entry : publisher_map_) {
    close(pub_entry.second.socket);
  }
}

EgdStatus EgdClient::Publish(const std::string& host, const uint16_t port, const EgdPageInfo& page_info,
    const uint8_t* data, const size_t data_len) {
  // We only support payloads up to 1400 bytes
  if (data_len > MAX_EGD_PAYLOAD) {
    return EgdStatus::ERR_MESSAGE_TOO_LARGE;
  }

  // If we haven't published on the requested page, connect and cache the socket
  const auto producer_id = page_info.producer_id;
  const auto exchange_id = page_info.exchange_id;
  const EgdPubKey publisher_key = {host, port, producer_id, exchange_id};
  if (publisher_map_.find(publisher_key) == publisher_map_.end()) {
    EgdPubInfo pub_info;
    EgdStatus status;

    // Attempt to connect, and if we fail, return early
    if ((status = EgdClient::PublishConnect(host, port, &pub_info)) != EgdStatus::OK) {
      return status;
    }
    publisher_map_[publisher_key] = pub_info;
  }

  // Get the current time so we can add it to the header
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);

  // Set up our message and our header
  auto& pub_info = publisher_map_[publisher_key];
  DataProductionMessage message;
  memset(&message, 0, sizeof(message));
  message.Hdr.PDUType = PDU_TYPE;
  message.Hdr.Version = PAYLOAD_VERSION;
  message.Hdr.ReqID = pub_info.request_id++;
  message.Hdr.ProducerID = page_info.producer_id;
  message.Hdr.ExchangeID = page_info.exchange_id;
  message.Hdr.Timestamp.tv_sec = now.tv_sec;
  message.Hdr.Timestamp.tv_nsec = now.tv_nsec;
  message.Hdr.ConfigTimeSecs = page_info.config_time.tv_sec;
  //message.Hdr.Status;  // TODO: Figure out what this can be set to
  message.Hdr.Signature = page_info.signature;
  memcpy(message.Data, data, data_len);

  // Attempt to publish the message
  if (sendto(pub_info.socket, &message, sizeof(message), 0, reinterpret_cast<sockaddr*>(&pub_info.addr), sizeof(pub_info.addr)) < sizeof(message) ) {
    return EgdStatus::ERR_PUBLISH;
  }

  return EgdStatus::OK;
}

void EgdClient::PollSocket(EgdSubInfo* sub_info) {
  while (run_flag_) {
    // Poll the socket to check if data is available
    pollfd poll_fd = {.fd = sub_info->socket, .events = POLLIN};
    if (poll(&poll_fd, 1, POLL_TIMEOUT_MS) < 1) {
      continue;
    }

    // Read from the socket
    socklen_t from_addr_len;
    sockaddr_in from_addr;
    DataProductionMessage message;
    const auto length = recvfrom(sub_info->socket, &message, sizeof(message), 0, reinterpret_cast<sockaddr*>(&from_addr), &from_addr_len);
    if (length > 0) {
      // Since each callback could want a different page, loop over each one
      pthread_rwlock_rdlock(&(sub_info->callbacks_lock));
      for (auto& callback_info : sub_info->callbacks) {
        // Make sure that this message is one that this callback cares about
        auto& page_info = callback_info.page_info;
        const auto& header = message.Hdr;
        if (header.ProducerID == page_info.producer_id && header.ExchangeID == page_info.exchange_id) {
          // If the signature is different from what we expect, fetch the config again
          EgdStatus status = OK;
          if (header.Signature != page_info.signature) {
            if ((status = reinterpret_cast<EgdConfig*>(page_info.config_handle)->Read(page_info.producer_id)) == EgdStatus::OK) {
              if ((status = reinterpret_cast<EgdConfig*>(page_info.config_handle)->GetPageInfo(page_info.page_name, &page_info)) == EgdStatus::OK) {
                if (header.Signature != page_info.signature) {
                  status = ERR_SIGNATURE_MISMATCH;
                }
              }
            }
          }

          // Finally, call the callback with the payload, and any errors that may have happened
          callback_info.callback(&page_info, &message.Hdr, status, message.Data, length - sizeof(message.Hdr), callback_info.user_data);
        }
      }
      pthread_rwlock_unlock(&sub_info->callbacks_lock);
    }
  }
}

EgdStatus EgdClient::Subscribe(const std::string& host, const uint16_t port, const EgdPageInfo& page_info,
    EgdSubCallback callback, void* user_data) {
  // If we haven't subscribed on the requested page, connect and cache the socket
  const EgdSubKey subscriber_key = {host, port};
  if (subscriber_map_.find(subscriber_key) == subscriber_map_.end()) {
    EgdSubInfo sub_info;
    EgdStatus status;

    // Attempt to connect, and if we fail, return early
    if ((status = EgdClient::SubscribeConnect(host, port, &sub_info)) != EgdStatus::OK) {
      return status;
    }

    // Start a thread that will poll the socket and call the callbacks
    subscriber_map_[subscriber_key] = sub_info;
    subscriber_map_[subscriber_key].p_thread = std::make_shared<std::thread>(&EgdClient::PollSocket, this, &(subscriber_map_[subscriber_key]));
  }

  // Lock the callbacks mutex and add the callback info
  auto& sub_info = subscriber_map_[subscriber_key];
  pthread_rwlock_wrlock(&sub_info.callbacks_lock);
  sub_info.callbacks.emplace_back(callback, page_info, user_data);
  pthread_rwlock_unlock(&sub_info.callbacks_lock);

  return EgdStatus::OK;
}
EgdStatus EgdClient::SubscribeBlocking(const std::string& host, const uint16_t port, const EgdPageInfo& page_info, EgdSubCallback callback, void* user_data) {
  // If we haven't subscribed on the requested page, connect and cache the socket
  const EgdSubKey subscriber_key = {host, port};
  EgdSubInfo sub_info;
  EgdStatus status;

  // Attempt to connect, and if we fail, return early
  if ((status = EgdClient::SubscribeConnect(host, port, &sub_info)) != EgdStatus::OK) {
    return status;
  }

  // Lock the callbacks mutex and add the callback info
  pthread_rwlock_wrlock(&sub_info.callbacks_lock);
  sub_info.callbacks.emplace_back(callback, page_info, user_data);
  pthread_rwlock_unlock(&sub_info.callbacks_lock);
  auto callback_f = sub_info.callbacks.front();
  PollSocket(&sub_info);

  close(sub_info.socket); // close the socket since we are done..

  return EgdStatus::OK;
}

}  // namespace client

}  // namespace egd


/*****************************
 * Begin C Definitions *
 ****************************/

using ::egd::client::EgdClient;

extern "C" {

EgdClientHandle* egd_client_init() {
  return reinterpret_cast<EgdClientHandle*>(new EgdClient());
}

EgdStatus egd_client_publish(EgdClientHandle *handle, const char *host, uint16_t port, const EgdPageInfo *page_info,
    const uint8_t* data, const size_t data_len) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<EgdClient*>(handle)->Publish(host, port, *page_info, data, data_len);
}

EgdStatus egd_client_subscribe(EgdClientHandle* handle, const char* host, uint16_t port, const EgdPageInfo* page_info,
    EgdSubCallback callback, void* user_data) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<EgdClient*>(handle)->Subscribe(host, port, *page_info, callback, user_data);
}

void egd_client_free(EgdClientHandle* handle) {
  delete reinterpret_cast<EgdClient*>(handle);
}

}
