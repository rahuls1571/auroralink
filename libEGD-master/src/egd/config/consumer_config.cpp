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
#include "egd/config/consumer_config.h"
#include "egd/config/consumer_config.hpp"

namespace egd {

namespace config {

static constexpr auto URL_CONSUMER_TYPE_VALUE = "ConsumedData";
static constexpr auto XML_CONSUMER_XPATH = "/ConsumedData/Consumer";
static constexpr auto XML_CONSUMED_EXCHANGE_XPATH_TEMPLATE = "/ConsumedData/Consumer/RequiredProducer[@ProducerId='%u']/ConsumedExchange";
static constexpr auto XML_BOUND_VAR_ATTRIBUTE = "BoundVar";

ConsumerConfig::ConsumerConfig(const std::string &host, uint16_t port) : EgdConfig(host, port) {
}

EgdStatus ConsumerConfig::Read(const uint32_t producer_id) {
  // Form the URLs for the request
  Url url = MakeBaseUrl(host_, port_, producer_id).add_query(URL_TYPE_KEY, URL_CONSUMER_TYPE_VALUE);

  // Fetch the XML and attempt to parse it
  std::string response;
  EgdStatus status;
  if ((status = GetRequest(url.str(), &response)) != EgdStatus::OK) {
    return status;
  }

  // Insert the query into the xpath so we only get the producer we are looking for
  const auto xpath_len = std::string(XML_CONSUMED_EXCHANGE_XPATH_TEMPLATE).size() + 10;
  char exchange_xpath[xpath_len];
  snprintf(exchange_xpath, xpath_len, XML_CONSUMED_EXCHANGE_XPATH_TEMPLATE, producer_id);

  // Parse the consumer XML
  if ((status = ParseXml(response, producer_id, XML_CONSUMER_XPATH, exchange_xpath, XML_BOUND_VAR_ATTRIBUTE)) != EgdStatus::OK) {
    return status;
  }

  return EgdStatus::OK;
}

}  // namespace config

}  // namespace egd


/*****************************
 * Begin C Definitions *
 ****************************/

using ::egd::config::ConsumerConfig;

extern "C" {

EgdConfigHandle* egd_consumer_config_init(const char* host, const uint16_t port) {
  return reinterpret_cast<EgdConfigHandle*>(new ConsumerConfig(host, port));
}

void egd_consumer_config_free(EgdConfigHandle* handle) {
  delete reinterpret_cast<ConsumerConfig*>(handle);
}

}