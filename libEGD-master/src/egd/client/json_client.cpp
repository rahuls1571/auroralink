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
#include <endian.h>
#include <unistd.h>
#include <mutex>
#include <iostream>
#include <cstring>
#include "egd/util/external/json/json.hpp"
#include "egd/config/egd_config.hpp"
#include "egd/client/json_client.hpp"

namespace egd {

namespace client {

using ::nlohmann::json;
using ::egd::config::EgdVarMap;

static constexpr auto BITS_PER_BYTE = 8;

EgdStatus JsonClient::Publish(const std::string& host, const uint16_t port, const EgdPageInfo& page_info,
    const std::string& message) {
  // Attempt to construct the JSON
  json message_json;
  try {
    message_json = json::parse(message);
  } catch (...) {
    return EgdStatus::ERR_JSON_PARSE;
  }

  // If we were passed a non-object JSON, we cannot continue
  if (!message_json.is_object()) {
    return EgdStatus::ERR_INVALID_JSON;
  }

  // Cast the variable map into the proper type
  const auto var_map = reinterpret_cast<EgdVarMap*>(page_info.var_map);

  // Loop over all the top level variables in the JSON, and attempt to find the variables in the page_info
  DataProductionMessage egd_message;  // This will be filled out in the below map so we can publish it
  memset(&egd_message, 0, sizeof(egd_message));  // Not sure if this is needed, but it makes it clearer that variables not present in the JSON are unset
  for (const auto& message_entry : message_json.items()) {
    // We are only able to process simple JSON key value pairs, so no arrays or objects
    const auto& value_json = message_entry.value();
    if (value_json.is_object() || value_json.is_array() || value_json.is_string()) {
      continue;
    }

    // Make sure that we have information on the requested variable
    const std::string& var_name = message_entry.key();
    if (var_map->find(var_name) == var_map->end()) {
      continue;
    }

    // Determine the byte and bit offset, then grab a pointer to where we will store the value
    const auto& var_info = var_map->at(var_name);
    const auto byte_offset = var_info.bit_offset / BITS_PER_BYTE;
    const auto bit_offset = var_info.bit_offset % BITS_PER_BYTE;

    // This is unlikely, but if we are near the end of the payload, and the type of the variable would put us over the end, skip it
    if (byte_offset >= MAX_EGD_PAYLOAD || (byte_offset + (var_info.data_type.bits / 8)) >= MAX_EGD_PAYLOAD) {
      return EgdStatus::ERR_OUT_OF_BOUNDS;
    }
    uint8_t* value_ptr = &(egd_message.Data[byte_offset]);

    // Depending on the type, serialize the value differently
    // But for each type of value larger than a byte, swap bytes to little endian if we need to
    switch (var_info.data_type.type) {
      case EGD_BOOL: {
        // Booleans are a special case where we will need to shift the value up to the proper bit and or with the current pointer to prevent overriding any other booleans
        bool val;
        if (value_json.is_boolean()) {
          val = value_json.get<bool>();
        } else {
          val = value_json.get<uint8_t>();
        }
        *value_ptr |= (val << bit_offset) & (0x01 << bit_offset);
        break;
      }
      case EGD_REAL: {
        const auto val = value_json.get<float>();
        const auto val_le = htole32(*reinterpret_cast<const uint32_t*>(&val));
        *reinterpret_cast<float*>(value_ptr) = *reinterpret_cast<const float*>(&val_le);
        break;
      }
      case EGD_LREAL: {
        const auto val = value_json.get<double>();
        const auto val_le = htole64(*reinterpret_cast<const uint64_t*>(&val));
        *reinterpret_cast<double*>(value_ptr) = *reinterpret_cast<const double*>(&val_le);
        break;
      }
      case EGD_SINT:
        *reinterpret_cast<int8_t*>(value_ptr) = value_json.get<int8_t>();
        break;
      case EGD_INT:
        *reinterpret_cast<int16_t*>(value_ptr) = htole16(value_json.get<int16_t>());
        break;
      case EGD_DINT:
        *reinterpret_cast<int32_t*>(value_ptr) = htole32(value_json.get<int32_t>());
        break;
      case EGD_LINT:
        *reinterpret_cast<int64_t*>(value_ptr) = htole64(value_json.get<int64_t>());
        break;
      case EGD_BYTE:
      case EGD_USINT:
        *value_ptr = value_json.get<uint8_t>();
        break;
      case EGD_WORD:
      case EGD_UINT:
        *reinterpret_cast<uint16_t*>(value_ptr) = htole16(value_json.get<uint16_t>());
        break;
      case EGD_DWORD:
      case EGD_UDINT:
        *reinterpret_cast<uint32_t*>(value_ptr) = htole32(value_json.get<uint32_t>());
        break;
      case EGD_ULINT:
        *reinterpret_cast<uint64_t*>(value_ptr) = htole64(value_json.get<uint64_t>());
        break;
      // The following types are not types that I am familiar with, so I have no idea how to serialize them
      // TODO(someone): Figure out if these are even valid, and how to serialize them from JSON
      case EGD_DT:
      case EGD_TIME:
      case EGD_STRING:
      case EGD_UNDEFINED:
      default:
        break;
    }
  }

  // Hand our formed message off to the egd client to publish
  return EgdClient::Publish(host, port, page_info, egd_message.Data, sizeof(egd_message.Data));
}

void JsonClient::JsonProxyCallback(const EgdPageInfo* page_info, const DataProductionHdr* header, const EgdStatus status,
    const uint8_t* data, size_t data_len, void* user_data) {
  // Cast the user data into the object that we will use to find the real callback and user data
  const auto real_user_data = reinterpret_cast<JsonUserData*>(user_data);

  // Cast the variable map into the proper type
  const auto var_map = reinterpret_cast<EgdVarMap*>(page_info->var_map);

  // If for some reason, the payload is smaller than the page is supposed to be, we can't do anything so just return
  if (data_len < page_info->data_length) {
    return;
  }

  // Loop over each of the variables in the page info, and attempt to pull them out of the payload into a JSON object
  json json_message;
  for (const auto& var_entry : *var_map) {
    const auto& var_name = var_entry.first;
    const auto byte_offset = var_entry.second.bit_offset / BITS_PER_BYTE;
    const auto bit_offset = var_entry.second.bit_offset % BITS_PER_BYTE;
    const uint8_t* value_ptr = &(data[byte_offset]);

    // Depending on the type, pull the data out of the payload differently
    const auto data_type = var_entry.second.data_type;
    switch (data_type.type) {
      case EGD_BOOL:
        json_message[var_name] = static_cast<bool>(((*value_ptr) >> bit_offset) & 0x01);
        break;
      case EGD_REAL: {
        const auto val = le32toh(*reinterpret_cast<const uint32_t*>(value_ptr));
        json_message[var_name] = *reinterpret_cast<const float*>(&val);
        break;
      }
      case EGD_LREAL: {
        const auto val = le64toh(*reinterpret_cast<const uint64_t*>(value_ptr));
        json_message[var_name] = *reinterpret_cast<const double*>(&val);
        break;
      }
      case EGD_SINT:
        json_message[var_name] = static_cast<int8_t>(*value_ptr);
        break;
      case EGD_INT:
        json_message[var_name] = static_cast<int16_t>(le16toh(*reinterpret_cast<const uint16_t*>(value_ptr)));
        break;
      case EGD_DINT:
        json_message[var_name] = static_cast<int32_t>(le32toh(*reinterpret_cast<const uint32_t*>(value_ptr)));
        break;
      case EGD_LINT:
        json_message[var_name] = static_cast<int64_t>(le64toh(*reinterpret_cast<const uint64_t*>(value_ptr)));
        break;
      case EGD_BYTE:
      case EGD_USINT:
        json_message[var_name] = *value_ptr;
        break;
      case EGD_WORD:
      case EGD_UINT:
        json_message[var_name] = le16toh(*reinterpret_cast<const uint16_t*>(value_ptr));
        break;
      case EGD_DWORD:
      case EGD_UDINT:

        json_message[var_name] = le32toh(*reinterpret_cast<const uint32_t*>(value_ptr));
        break;
      case EGD_ULINT:
        json_message[var_name] = le64toh(*reinterpret_cast<const uint64_t*>(value_ptr));
        break;
        // The following types are not types that I am familiar with, so I have no idea how to serialize them
        // TODO(someone): Figure out if these are even valid, and how to serialize them from JSON
      case EGD_DT:
      case EGD_TIME:
      case EGD_STRING:
      case EGD_UNDEFINED:
      default:
        break;
    }
  }

  // Finally call the callback with the formed JSON
  real_user_data->callback(page_info, header, status, json_message.dump().c_str(), real_user_data->user_data);
}

EgdStatus JsonClient::Subscribe(const std::string& host, const uint16_t port, const EgdPageInfo& page_info,
    EgdJsonSubCallback callback, void* user_data) {
  // All we are really going to do in here, is store the user data and call the normal subscribe function with our custom callback
  json_user_data_.push_back(std::make_shared<JsonUserData>(callback, user_data));
  return EgdClient::Subscribe(host, port, page_info, JsonClient::JsonProxyCallback, reinterpret_cast<void*>(&*json_user_data_.back()));
}

EgdStatus JsonClient::SubscribeBlocking(const std::string& host, const uint16_t port, const EgdPageInfo& page_info,
    EgdJsonSubCallback callback, void* user_data) {
  // All we are really going to do in here, is store the user data and call the normal subscribe function with our custom callback
  json_user_data_.push_back(std::make_shared<JsonUserData>(callback, user_data));
  return EgdClient::SubscribeBlocking(host, port, page_info, JsonClient::JsonProxyCallback, reinterpret_cast<void*>(&*json_user_data_.back()));
}

}  // namespace client

}  // namespace egd


/*****************************
 * Begin C Definitions *
 ****************************/

using ::egd::client::JsonClient;

extern "C" {

EgdJsonClientHandle* egd_json_client_init() {
  return reinterpret_cast<EgdJsonClientHandle*>(new JsonClient());
}

EgdStatus egd_json_client_publish(EgdJsonClientHandle* handle, const char* host, uint16_t port, const EgdPageInfo* page_info,
    const char* message) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<JsonClient*>(handle)->Publish(host, port, *page_info, message);
}

EgdStatus egd_json_client_subscribe(EgdJsonClientHandle* handle, const char* host, uint16_t port, const EgdPageInfo* page_info,
    EgdJsonSubCallback callback, void* user_data) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<JsonClient*>(handle)->Subscribe(host, port, *page_info, callback, user_data);
}

void egd_json_client_free(EgdJsonClientHandle* handle) {
  delete reinterpret_cast<JsonClient*>(handle);
}

}
