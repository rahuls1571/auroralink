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
#include <algorithm>
#include <functional>
#include <curl/curl.h>
#include "egd/util/external/pugixml/pugixml.hpp"
#include "egd/config/egd_config.hpp"

namespace egd {

namespace config {

using pugi::xml_document;
using pugi::xml_parse_result;
using pugi::xpath_node;
using pugi::xpath_node_set;
using pugi::xml_node;
using pugi::xml_attribute;
using pugi::status_ok;

static constexpr auto URL_SCHEME = "http";
static constexpr auto URL_PATH = "/EGD";
static constexpr auto URL_ACTION_KEY = "Action";
static constexpr auto URL_ACTION_VALUE = "GetDoc";
static constexpr auto URL_ACTIVE_KEY = "Active";
static constexpr auto URL_ACTIVE_VALUE = "true";
static constexpr auto URL_PRODUCER_ID_KEY = "ProducerID";
static constexpr auto XML_CONFIG_PRODUCER_NAME_ATTRIBUTE = "Name";
static constexpr auto XML_CONFIG_TIME_SECS_ATTRIBUTE = "ConfigTimeSecs";
static constexpr auto XML_CONFIG_TIME_N_SECS_ATTRIBUTE = "ConfigTimeNSecs";
static constexpr auto XML_PERIOD_SECS_ATTRIBUTE = "PeriodSecs";
static constexpr auto XML_PERIOD_N_SECS_ATTRIBUTE = "PeriodNSecs";
static constexpr auto XML_DATA_LENGTH_ATTRIBUTE = "DataLength";
static constexpr auto XML_PAGE_ATTRIBUTE = "Page";
static constexpr auto XML_EXCHANGE_ID_ATTRIBUTE = "ExchangeId";
static constexpr auto XML_SIG_MAJOR_ATTRIBUTE = "SigMajor";
static constexpr auto XML_SIG_MINOR_ATTRIBUTE = "SigMinor";
static constexpr auto XML_NAME_ATTRIBUTE = "Name";
static constexpr auto XML_VOFFS_ATTRIBUTE = "VOffs";
static constexpr auto XML_DTYPE_ATTRIBUTE = "DType";

// We will use this map to determine the type of variable found in the XML and it's size
static std::map<std::string, EgdTypeInfo> EgdTypeNameMap = {
    {"UNDEFINED", {EGD_UNDEFINED, -1}},
    {"BOOL", {EGD_BOOL, 1}},
    {"REAL", {EGD_REAL, 32}},
    {"LREAL", {EGD_LREAL, 64}},
    {"SINT", {EGD_SINT, 8}},
    {"INT", {EGD_INT, 16}},
    {"DINT", {EGD_DINT, 32}},
    {"LINT", {EGD_LINT, 64}},
    {"USINT", {EGD_USINT, 8}},
    {"UINT", {EGD_UINT, 16}},
    {"UDINT", {EGD_UDINT, 32}},
    {"ULINT", {EGD_ULINT, 64}},
    {"BYTE", {EGD_BYTE, 8}},
    {"WORD", {EGD_WORD, 16}},
    {"DWORD", {EGD_DWORD, 32}},
    {"DT", {EGD_DT, 0}},
    {"TIME", {EGD_TIME, 0}},
    {"STRING", {EGD_STRING, 0}},
};

EgdConfig::EgdConfig(const std::string& host, uint16_t port) : host_(host), port_(port) {
}

Url EgdConfig::MakeBaseUrl(const std::string& host, const uint16_t port, const uint32_t producer_id) {
  Url base_url;
  return base_url
      .scheme(URL_SCHEME)
      .host(host)
      .port(port)
      .path(URL_PATH)
      .add_query(URL_ACTION_KEY, URL_ACTION_VALUE)
      .add_query(URL_ACTIVE_KEY, URL_ACTIVE_VALUE)
      .add_query(URL_PRODUCER_ID_KEY, std::to_string(producer_id));
}

EgdStatus EgdConfig::GetRequest(const std::string& url, std::string* response) {
  // Make sure that we were not passed a NULL pointer
  if (response == nullptr) {
    return EgdStatus::ERR_NULL;
  }

  // If we were unable to init the handle, return an error
  auto curl = curl_easy_init();
  if (curl == nullptr) {
    curl_easy_cleanup(curl);
    return EgdStatus::ERR_CURL_INIT;
  }

  // Set the URL for the request
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  // Allow the 100 continue to wait for 60 seconds before it fails
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_EXPECT_100_TIMEOUT_MS, 60000L);

  // Allocate a string, and read the response into it
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, EgdConfig::EgdConfigReadResponseCallback);

  // Perform the request, and if it fails, log an error
  int rc = curl_easy_perform(curl);

  // We can safely clean up the curl request now
  curl_easy_cleanup(curl);

  // If we were unable to curl, return early
  if (rc != CURLE_OK) {
    return EgdStatus::ERR_CURL_REQUEST;
  }

  return EgdStatus::OK;
}

size_t EgdConfig::EgdConfigReadResponseCallback(void *ptr, size_t size, size_t nmemb, std::string* response) {
  // Compute the size of the response
  const size_t response_len = size * nmemb;

  // Initialize the string with the response, and length
  *response += std::string((char*)ptr, response_len);

  // Return the size of the response
  return response_len;
}

EgdStatus EgdConfig::ParseXml(const std::string& xml_str, const uint32_t producer_id,
    const std::string& producer_xpath, const std::string& exchange_xpath, const std::string& var_attribute) {
  // Load the string into an XML document and return if this fails
  xml_document doc;
  const auto rc = doc.load_string(xml_str.c_str());
  if (rc.status != pugi::status_ok) {
    return EgdStatus::ERR_XML_PARSE;
  }

  // Attempt to find the consumer piece of the XML
  const auto& producer = doc.select_node(producer_xpath.c_str());
  const auto& producer_node = producer.node();
  if (producer_node.empty()) {
    return EgdStatus::ERR_XML_PRODUCER_NOT_FOUND;
  }

  // Attempt to find the consumed exchanges out of the XML
  const auto& exchanges = doc.select_nodes(exchange_xpath.c_str());
  if (exchanges.empty()) {
    return EgdStatus::ERR_XML_EXCHANGE_NOT_FOUND;
  }

  // Attempt to find the producer name
  producer_name_ = producer_node.attribute(XML_CONFIG_PRODUCER_NAME_ATTRIBUTE).as_string();

  // Attempt to find the config time for this producer
  config_time_.tv_sec = producer_node.attribute(XML_CONFIG_TIME_SECS_ATTRIBUTE).as_uint();
  config_time_.tv_nsec = producer_node.attribute(XML_CONFIG_TIME_N_SECS_ATTRIBUTE).as_uint();

  // Loop over all of the exchanges to populate the map
  for (const auto& exchange : exchanges) {
    // std::cout << "exchange : " << exchange << std::endl;
    const auto& exchange_node = exchange.node();
    if (exchange_node.empty()) {
      continue;
    }
    std::string page_name_builder = exchange_node.attribute(XML_PAGE_ATTRIBUTE).as_string();
    std::string exchange_id_string = exchange_node.attribute(XML_EXCHANGE_ID_ATTRIBUTE).as_string();
    page_name_builder.append(exchange_id_string);

    const std::string& page_name = page_name_builder;

    // Extract all the static information into the page information
    EgdPageMapEntry page_entry;
    // Dont like this, but unavoidable as we need to copy the page name into this structure, otherwise on the next loop iteration
    //  we will overwrite the previous pointer, and end up with two object that have the same page name with different contents..
    page_entry.page.page_name = (char*)malloc(page_name.length());
    std::memcpy(page_entry.page.page_name, (void*)page_name.c_str(), page_name.length());
    page_entry.page.producer_name = (char*)producer_name_.c_str();
    page_entry.page.producer_id = producer_id;
    page_entry.page.exchange_id = exchange_node.attribute(XML_EXCHANGE_ID_ATTRIBUTE).as_ullong();
    page_entry.page.sig_major = exchange_node.attribute(XML_SIG_MAJOR_ATTRIBUTE).as_uint();
    page_entry.page.sig_minor = exchange_node.attribute(XML_SIG_MINOR_ATTRIBUTE).as_uint();
    page_entry.page.signature = (page_entry.page.sig_major << 8) | (page_entry.page.sig_minor & 0xFF);
    page_entry.page.period.tv_sec = exchange_node.attribute(XML_PERIOD_SECS_ATTRIBUTE).as_uint();
    page_entry.page.period.tv_nsec = exchange_node.attribute(XML_PERIOD_N_SECS_ATTRIBUTE).as_uint();
    page_entry.page.config_time.tv_sec = exchange_node.attribute(XML_CONFIG_TIME_SECS_ATTRIBUTE).as_uint();
    page_entry.page.config_time.tv_nsec = exchange_node.attribute(XML_CONFIG_TIME_N_SECS_ATTRIBUTE).as_uint();
    page_entry.page.data_length = exchange_node.attribute(XML_DATA_LENGTH_ATTRIBUTE).as_uint();
    page_entry.page.config_handle = reinterpret_cast<EgdConfigHandle*>(this);

    // Initialize the variable map
    page_entry.var_map = std::make_shared<EgdVarMap>();

    // Check if the page name has already been added to the vector, and if so get the page name, otherwise add it
    // const auto& page_name_it = std::find(page_names_.begin(), page_names_.end(), page_name);
    // if (page_name_it != page_names_.end()) {
    //   page_entry.page.page_name = (char*)((*page_name_it).c_str());
    // } else {
    //   page_names_.push_back(page_name);
    //   const auto& page_name_ref = page_names_.back();
    //   page_entry.page.page_name = (char*)(page_name_ref.c_str());
    // }

    // Loop over each of the children to populate the inner map
    for (const auto& exchange_var : exchange_node.children(var_attribute.c_str())) {
      // Store the variable information in the page map entry
      EgdVarInfo var;
      var.bit_offset = exchange_var.attribute(XML_VOFFS_ATTRIBUTE).as_ullong();
      var.data_type = EgdTypeNameMap[exchange_var.attribute(XML_DTYPE_ATTRIBUTE).as_string()];

      const auto& variable_name = exchange_var.attribute(XML_NAME_ATTRIBUTE).as_string();
      (*(page_entry.var_map))[variable_name] = var;
    }

    // Save the page entry into the passed map
    // TODO: this is the issue, if we get a page with multiple exchanges in it, this will overwrite the first entry with the last one.

    page_map_.insert(std::pair<std::string, EgdPageMapEntry>(page_name, page_entry));
  }

  return EgdStatus::OK;
}

EgdStatus EgdConfig::GetConfigTime(DataTime* config_time) const {
  if (config_time == nullptr) {
    return EgdStatus::ERR_NULL;
  }

  config_time->tv_nsec = config_time_.tv_nsec;
  config_time->tv_sec = config_time_.tv_sec;
  return EgdStatus::OK;
}

EgdStatus EgdConfig::GetPageMap(egd::config::EgdPageMap* page_map) const {
  *page_map = page_map_;
  return EgdStatus::OK;
}

EgdStatus EgdConfig::GetPageInfo(const std::string& page_name, EgdPageInfo* page_info) const {
  // If we were passed a nullptr, return instead of seg faulting
  if (page_info == nullptr) {
    return EgdStatus::ERR_NULL;
  }

  // Make sure that we have information on the requested page
  if (page_map_.find(page_name) == page_map_.end()) {
    return EgdStatus::ERR_PAGE_NOT_FOUND;
  }

  // Pull the information out of the page map (minus the var_map)
  const auto& page_entry = page_map_.at(page_name);
  memcpy(page_info, &(page_entry.page), sizeof(EgdPageInfo) - sizeof(EgdPageInfo::var_map));

  // Assign a pointer to the var_map into the page info
  page_info->var_map = reinterpret_cast<EgdVarMapHandle*>(&*(page_entry.var_map));

  return EgdStatus::OK;
}

EgdStatus EgdConfig::GetVarInfo(const std::string& var_name, const EgdPageInfo& page_info, EgdVarInfo* var_info) const {
  // If we were passed a nullptr, return instead of seg faulting
  if (var_info == nullptr) {
    return EgdStatus::ERR_NULL;
  }

  // Make sure that we have information on the requested variable
  const auto page_var_map = reinterpret_cast<EgdVarMap*>(page_info.var_map);
  if (page_var_map->find(var_name) == page_var_map->end()) {
    return EgdStatus::ERR_VAR_NOT_FOUND;
  }

  // Pull the information out of the variable map
  memcpy(var_info, &(page_var_map->at(var_name)), sizeof(EgdVarInfo));
  return EgdStatus::OK;
}

void EgdConfig::DumpPageMap() {
  for (const auto& page_entry : page_map_) {
    const auto& page = page_entry.second.page;
    std::cout << "============================" << std::endl;
    std::cout << "Page:                     " << page_entry.first << std::endl;
    std::cout << "  Producer ID:              " << page.producer_id << std::endl;
    std::cout << "  Exchange ID:              " << page.exchange_id << std::endl;
    std::cout << "  Sig Major:                " << (int)page.sig_major << std::endl;
    std::cout << "  Sig Minor:                " << (int)page.sig_minor << std::endl;
    std::cout << "  Period Seconds:           " << page.period.tv_sec << std::endl;
    std::cout << "  Period Nanoseconds:       " << page.period.tv_nsec << std::endl;
    std::cout << "  Config Time Seconds:      " << page.config_time.tv_sec << std::endl;
    std::cout << "  Config Time Nanoseconds:  " << page.config_time.tv_nsec << std::endl;
    std::cout << "  Data Length:              " << page.data_length << std::endl;
    std::cout << "--" << std::endl;
    for (const auto& var_entry : *(page_entry.second.var_map)) {
      std::cout << "----" << std::endl;
      std::cout << "    Var:        " << var_entry.first << std::endl;
      std::cout << "      Bit Offset: " << var_entry.second.bit_offset << std::endl;
    }
  }
}

}  // namespace config

}  // namespace egd


/*****************************
 * Begin C Definitions *
 ****************************/

using ::egd::config::EgdConfig;

extern "C" {

EgdStatus egd_config_read(EgdConfigHandle* handle, const uint32_t producer_id) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<EgdConfig*>(handle)->Read(producer_id);
}

EgdStatus egd_config_get_config_time(const EgdConfigHandle* handle, DataTime* config_time) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<const EgdConfig*>(handle)->GetConfigTime(config_time);
}

EgdStatus egd_config_get_page_info(const EgdConfigHandle* handle, const char* page_name, struct EgdPageInfo* info) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<const EgdConfig*>(handle)->GetPageInfo(page_name, info);
}

EgdStatus egd_config_get_var_info(const EgdConfigHandle* handle, const char* var_name, const EgdPageInfo* page_info,
    struct EgdVarInfo* var_info) {
  if (handle == nullptr) {
    return EgdStatus::ERR_NULL;
  }
  return reinterpret_cast<const EgdConfig*>(handle)->GetVarInfo(var_name, *page_info, var_info);
}

}
