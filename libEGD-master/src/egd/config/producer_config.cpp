/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include <functional>
#include <algorithm>
#include "egd/config/producer_config.hpp"

#include <iostream>

namespace egd {

namespace config {

static constexpr auto URL_PRODUCER_TYPE_VALUE = "ProducedData";
static constexpr auto XML_PRODUCER_XPATH = "/ProducedData/Producer";
static constexpr auto XML_EXCHANGE_XPATH = "/ProducedData/Producer/Exchange";
static constexpr auto XML_VAR_ATTRIBUTE = "Var";

ProducerConfig::ProducerConfig(const std::string& host, const uint16_t port) : EgdConfig(host, port) {
}

EgdStatus ProducerConfig::Read(const uint32_t producer_id) {
  // Form the URLs for the request
  Url url = MakeBaseUrl(host_, port_, producer_id).add_query(URL_TYPE_KEY, URL_PRODUCER_TYPE_VALUE);

  // Fetch the producer XML and attempt to parse it
  std::string response;
  EgdStatus status;
  if ((status = GetRequest(url.str(), &response)) != EgdStatus::OK) {
    return status;
  }

  if ((status = ParseXml(response, producer_id, XML_PRODUCER_XPATH, XML_EXCHANGE_XPATH, XML_VAR_ATTRIBUTE)) != EgdStatus::OK) {
    return status;
  }

  return EgdStatus::OK;
}

}  // namespace config

}  // namespace egd


/*****************************
 * Begin C Definitions *
 ****************************/

using ::egd::config::ProducerConfig;

extern "C" {

EgdConfigHandle* egd_producer_config_init(const char* host, const uint16_t port) {
  return reinterpret_cast<EgdConfigHandle*>(new ProducerConfig(host, port));
}

void egd_producer_config_free(EgdConfigHandle* handle) {
  delete reinterpret_cast<ProducerConfig*>(handle);
}

}
