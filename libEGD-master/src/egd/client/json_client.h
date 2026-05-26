/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CLIENT_JSON_CLIENT_H_
#define _LIBEGD_EGD_CLIENT_JSON_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "egd/config/egd_config.h"

/**
 * @brief Opaque pointer to our C++ class
 */
struct _EgdJsonClientHandle;
typedef struct _EgdJsonClientHandle EgdJsonClientHandle;

/**
 * @brief Function signature for subscriptions to the client for pages
 * @param page_info Information on the page requested for this subscription
 * @param header The header of the message that was just received
 * @param status The status of the subscription read calls that the client can use to determine if the data is valid
 * @param message The message containing the page data (in JSON)
 * @param user_data User data passed in at time of subscribe
 */
typedef void(*EgdJsonSubCallback)(const struct EgdPageInfo* page_info, const struct DataProductionHdr* header,
    const EgdStatus status, const char* message, void* user_data);

/**
 * @brief Initialize the EGD JSON client
 * @return An initialized EGD JSON client
 */
EgdJsonClientHandle* egd_json_client_init();

/**
 * @brief Publishes a message using an EGD client handle
 * @param handle The handle to use to publish
 * @param host The host to publish to
 * @param port The port to publish to
 * @param page_info Information on the page that you are going to publish to
 * @param message The message that you want to publish (in JSON
 * @return EgdStatus reflecting the result
 */
EgdStatus egd_json_client_publish(EgdJsonClientHandle* handle, const char* host, uint16_t port,
    const struct EgdPageInfo* page_info, const char* message);

/**
 * @brief Subscribes to messages using an EGD client handle
 * @param handle The handle to use to subscribe
 * @param host The host to subscribe on
 * @param port The port to subscribe on
 * @param page_info Information on the page that you are going to subscribe to
 * @param callback The callback that will be called when a message is received
 * @param user_data Arbitrary user data that will be passed to the callback
 * @return EgdStatus reflecting the result
 */
EgdStatus egd_json_client_subscribe(EgdJsonClientHandle* handle, const char* host, uint16_t port,
    const struct EgdPageInfo* page_info, EgdJsonSubCallback callback, void* user_data);

/**
 * @brief Frees the EGD client
 * @param handle The handle to free
 */
void egd_json_client_free(EgdJsonClientHandle* handle);

#ifdef __cplusplus
}
#endif

#endif  // _LIBEGD_EGD_CLIENT_JSON_CLIENT_H_
