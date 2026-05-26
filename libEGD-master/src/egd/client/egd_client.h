/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CLIENT_EGD_CLIENT_H_
#define _LIBEGD_EGD_CLIENT_EGD_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "egd/config/egd_config.h"

/**
 * @brief Opaque pointer to our C++ class
 */
struct _EgdClientHandle;
typedef struct _EgdClientHandle EgdClientHandle;

/**
 * @brief Function signature for subscriptions to the client for pages
 * @param page_info Information on the page requested for this subscription
 * @param header The header of the message that was just received
 * @param status The status of the subscription read calls that the client can use to determine if the data is valid
 * @param data The data section of the message that you want to publish (not including the header)
 * @param data_len The length of the data in bytes (must be <= 1400)
 * @param user_data User data passed in at time of subscribe
 */
typedef void(*EgdSubCallback)(const struct EgdPageInfo* page_info, const struct DataProductionHdr* header,
    const EgdStatus status, const uint8_t* data, size_t data_len, void* user_data);

/**
 * @brief Initialize the EGD client
 * @return An initialized EGD client
 */
EgdClientHandle* egd_client_init();

/**
 * @brief Publishes a message using an EGD client handle
 * @param handle The handle to use to publish
 * @param host The host to publish to
 * @param port The port to publish to
 * @param page_info Information on the page that you are going to publish to
 * @param data The data section of the message that you want to publish (not including the header)
 * @param data_len The length of the data in bytes (must be <= 1400)
 * @return EgdStatus reflecting the result
 */
EgdStatus egd_client_publish(EgdClientHandle* handle, const char* host, uint16_t port,
    const struct EgdPageInfo* page_info, const uint8_t* data, size_t data_len);

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
EgdStatus egd_client_subscribe(EgdClientHandle* handle, const char* host, uint16_t port,
    const struct EgdPageInfo* page_info, EgdSubCallback callback, void* user_data);

/**
 * @brief Frees the EGD client
 * @param handle The handle to free
 */
void egd_client_free(EgdClientHandle* handle);

#ifdef __cplusplus
}
#endif

#endif  // _LIBEGD_EGD_CLIENT_EGD_CLIENT_H_
