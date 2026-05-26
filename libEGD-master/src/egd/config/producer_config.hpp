/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CONFIG_PRODUCER_CONFIG_HPP_
#define _LIBEGD_EGD_CONFIG_PRODUCER_CONFIG_HPP_

#include <map>
#include <unordered_map>
#include <chrono>  // NOLINT(build/c++11)
#include <vector>
#include <string>
#include "egd/util/egd_spec.h"
#include "egd/config/egd_config.hpp"

namespace egd {

namespace config {

/**
 * @brief C++ class that will implement the required functionality in order to query the EGD server for information about pages
 */
class ProducerConfig : public EgdConfig {
 public:
  /**
   * @brief Constructor that will initialize the list of available variables
   * @param host The host of the EGD server to connect to
   * @param port The port of the EGD server to connect to
   */
  ProducerConfig(const std::string& host, uint16_t port);

  /**
   * @brief Reads the config from an HTTP server
   * @param producer_id The producer_id we should request from the EGD server
   * @return EgdConfigStatus that will report the status
   */
  EgdStatus Read(uint32_t producer_id) override;
};

}  // namespace config

}  // namespace egd

#endif  // _LIBEGD_EGD_CONFIG_PRODUCER_CONFIG_HPP_
