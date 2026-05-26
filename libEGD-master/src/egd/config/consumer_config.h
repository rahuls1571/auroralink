/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CONFIG_CONSUMER_CONFIG_H_
#define _LIBEGD_EGD_CONFIG_CONSUMER_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "egd/config/egd_config.h"

/**
 * @brief Initializes the consumer config object
 * @param host The host to connect to in order to read the config
 * @param port The port to connect to in order to read the config
 * @return Handle to an initialized consumer config object.
 */
EgdConfigHandle* egd_consumer_config_init(const char* host, uint16_t port);

/**
 * @brief Frees an initialized consumer config handle
 * @param handle The handle to free
 */
void egd_consumer_config_free(EgdConfigHandle* handle);

#ifdef __cplusplus
}
#endif

#endif  // _LIBEGD_EGD_CONFIG_CONSUMER_CONFIG_H_
