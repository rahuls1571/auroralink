/*
 * Copyright (c) 2017 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CONFIG_EGD_CONFIG_HPP_
#define _LIBEGD_EGD_CONFIG_EGD_CONFIG_HPP_

#include <map>
#include <memory>
#include <unordered_map>
#include <chrono>  // NOLINT(build/c++11)
#include <vector>
#include <string>
#include "egd/util/egd_spec.h"
#include "egd/util/external/CxxUrl/url.hpp"
#include "egd/config/egd_config.h"

namespace egd {

namespace config {

static constexpr auto URL_TYPE_KEY = "Type";

/**
 * @brief Map that will store information on variable names and their information
 */
using EgdVarMap = std::map<std::string, EgdVarInfo>;

/**
 * @brief Maps a page to a set of variables
 */
struct EgdPageMapEntry {
  EgdPageInfo page;  /// The page that this variable map describes
  std::shared_ptr<EgdVarMap> var_map;  /// Shared pointer for a mapping of variables for this page
};

/**
 * @brief Map that will store information on page names and their information
 */
using EgdPageMap = std::map<std::string, EgdPageMapEntry>;

/**
 * @brief C++ class that will implement the required functionality in order to query the EGD server for information about pages
 */
class EgdConfig {
 protected:
  std::string host_;  /// The host that this object will attempt to query the HTTP server for config
  uint16_t port_;  /// The port that this object will attempt to connect to on the hsot to ge the config
  DataTime config_time_;  /// The time that this piece of config was configured
  std::string producer_name_;  /// Name of the producer that this config came from. Keep track of this so that we can safely set the string in the EgdPageInfo struct without an additional malloc
  std::vector<std::string> page_names_;  /// Names of pages that we have fetched from the config, we keep track of them so we can set the page name in the page info struct
  EgdPageMap page_map_;  /// This map will be populated with information about pages and their variables

  /**
   * @brief Forms the base URL used to fetch the XML from an HTTP server
   * @param host The host to add to the URL
   * @param port The port to add to the URL
   * @param producer_id The producer_id to add to the URL
   * @return URL object containing the base required parameters to
   */
  Url MakeBaseUrl(const std::string& host, uint16_t port, uint32_t producer_id);

  /**
   * @brief Helper function that will perform a GET request and store the response
   * @param url The url to GET from
   * @param response Non NULL pointer to populate with the response
   * @return EgdStatus that will report the status
   */
  EgdStatus GetRequest(const std::string& url, std::string* response);

  /**
   * @brief Callback used by curl to read the response from the http request
   * @param ptr Chunk of data returned from the request
   * @param size The size of the response
   * @param nmemb The number of bytes in the chunk
   * @param response User data passed by us to be filled out
   * @return The number of bytes handled
   */
  static size_t EgdConfigReadResponseCallback(void *ptr, size_t size, size_t nmemb, std::string* response);

  /**
   * @brief Helper function that will attempt to parse the production XML
   * @param xml_str The XML in string format
   * @param producer_id Producer ID of the XML that was fetched
   * @param producer_xpath Xpath to the producer or consumer
   * @param exchange_xpath Xpath to the exchange
   * @param var_attribute Name of the attribute that will contain the variables
   * @param page_map The map to populate in this function
   * @return EgdStatus that will report the status
   */
  EgdStatus ParseXml(const std::string& xml_str, uint32_t producer_id, const std::string& producer_xpath,
      const std::string& exchange_xpath, const std::string& var_attribute);

 public:
  /**
   * @brief Constructor that will initialize the list of available variables
   */
  EgdConfig(const std::string& host, uint16_t port);

  /**
   * @brief Destructor that should be overridden by any implementing class that needs to clean up
   */
  virtual ~EgdConfig() = default;

  /**
   * @brief Reads the config from an HTTP server
   * @param producer_id The producer_id we should request from the EGD server
   * @return EgdStatus that will report the status
   */
  virtual EgdStatus Read(uint32_t producer_id) = 0;

  /**
   * @brief Gets the config time for this config object
   * @param config_time The time since epoch that this object was configured
   * @return EgdStatus that will report the status (always OK for now)
   */
  EgdStatus GetConfigTime(DataTime* config_time) const;

  /**
   * @brief Gets a reference to the entire EGD page map
   * @param page_map Reference to the page map that will be filled out by this function
   * @return EgdStatus that will report the status (always OK for now)
   */
  EgdStatus GetPageMap(EgdPageMap* page_map) const;

  /**
   * @brief Gets info on a single page given a page name
   * @param page_name The name of the page to fetch information on
   * @param page_info Struct that will be populated with info about the page
   * @warning The info field passed back is only valid until this object is destroyed.
   * @return EgdStatus that will report the status
   */
  EgdStatus GetPageInfo(const std::string& page_name, EgdPageInfo* page_info) const;

  /**
   * @brief Looks up the production variable information in the internal map
   * @param var_name The name of the variable to look up
   * @param page_info Information on the Page that contains the requested variable
   * @param var_info Pointer to a non-NULL var_ref_t struct that will be populated with info
   * @return EgdStatus that will report the status
   */
  EgdStatus GetVarInfo(const std::string& var_name, const EgdPageInfo& page_info, EgdVarInfo* var_info) const;

  /**
   * @brief Dumps the Page map to stdout for debug purposes
   */
  void DumpPageMap();
};

}  // namespace config

}  // namespace egd

#endif  // _LIBEGD_EGD_CONFIG_EGD_CONFIG_HPP_
