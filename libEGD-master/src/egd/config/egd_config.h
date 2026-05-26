/*
 * Copyright (c) 2019 General Electric Company. All rights reserved.
 *
 * The copyright to the computer software herein is the property of
 * General Electric Company. The software may be used and/or copied only
 * with the written permission of General Electric Company or in accordance
 * with the terms and conditions stipulated in the agreement/contract
 * under which the software has been supplied.
 */
#ifndef _LIBEGD_EGD_CONFIG_EGD_CONFIG_H_
#define _LIBEGD_EGD_CONFIG_EGD_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "egd/util/egd_spec.h"
#include "egd/util/egd_status.h"

/**
 * @brief Opaque pointer to our C++ class
 */
struct _EgdConfigHandle;
typedef struct _EgdConfigHandle EgdConfigHandle;

/**
 * @brief Opaque pointer to the map that contains variable information
 * @warning Any pointers to this object made from a config handle will no longer be valid once that config has been freed
 */
struct _EgdVarMapHandle;
typedef struct _EgdVarMapHandle EgdVarMapHandle;

/**
 * @brief We will use this enum to represent the types that the XML documents for EGD types
 */
enum EgdDataType {
  EGD_UNDEFINED = -1,  /// Represents no type or an unknown type.
  EGD_BOOL = 0,        /// Bits: 1    Range: [0,1]
  EGD_REAL = 1,        /// Real Numbers, Bits: 32, Range: [3.402823E+38,…,0, 1.401298E-45, 3.402823E+38]
  EGD_LREAL = 2,       /// Long Real Numbers, Bits: 64    Range: [-1.79769313486231E+308,…,0,  4.94065645841247E-324,…,  1.79769313486231E+308]
  EGD_SINT = 3,        /// Short Integer, Bits: 8    Range: -128,…,127
  EGD_INT = 4,         /// Integer, Bits: 16    Range: [-32768,…,+32767]
  EGD_DINT = 5,        /// Double Integer, Bits: 32    Range: –231, …231-1
  EGD_LINT = 6,        /// Long Integer, Bits: 64    Range: [-263,…,263-1]
  EGD_USINT = 7,       /// Unsigned Short Integer, Bits: 8    Range: 0,…,255
  EGD_UINT = 8,        /// Unsigned Integer, Bits: 16    Range: 0,…,65535
  EGD_UDINT = 9,       /// Unsigned Double Integer, Bits: 32    Range: 0,…,232-1
  EGD_ULINT = 10,      /// Unsigned Long Integer, Bits: 64    Range: 0,…,264-1
  EGD_BYTE = 11,       /// Bit String 8, Bits: 8    Range: 0,…,0xFF
  EGD_WORD = 12,       /// Bit String 16, Bits: 16    Range: 0,…,0xFFFF
  EGD_DWORD = 13,      /// Bit String 32, Bits: 32    Range: 0,…,0xFFFFFFFF
  EGD_DT = 14,         /// Date and Time,
  EGD_TIME = 15,       /// Duration
  EGD_STRING = 16,     /// Character String.
};

/**
 * @brief This struct will hold information on a specific EGD page
 */
struct EgdPageInfo {
  DWORD producer_id;  /// The producer ID that this page should be read from
  DWORD exchange_id;  /// The Exchange ID that this page should be read from
  BYTE sig_major;  /// The top 8 bits of the signature
  BYTE sig_minor;  /// The bottom 8 bits of the signature
  WORD signature;  /// The combined signature
  struct DataTime period;  /// The period in seconds, and nanoseconds
  struct DataTime config_time;  /// The config time in seconds and nanoseconds
  DWORD data_length;  /// The length of the data on this page

  // The following variables are dependent on the config object that produced this struct existing, if it gets deleted, they will also get deleted
  char* producer_name;  /// The name of the producer that this page is from
  char* page_name;  /// The name of the page that this information is about
  EgdConfigHandle* config_handle;  /// Handle to the config object that initialized this page info
  EgdVarMapHandle* var_map;  /// Handle to the map of variables contained in this page
};

/**
 * @brief This struct will hold information about the type of a variable
 */
struct EgdTypeInfo {
  enum EgdDataType type;  /// Enum representing the type of data which maps to the EGD representation
  int bits;  /// The size of the datatype in bits
};

/**
 * @brief This struct will hold important information about the referenced variable
 */
struct EgdVarInfo {
  DWORD bit_offset;  /// The offset in bits of the variable
  struct EgdTypeInfo data_type;  /// The data type of the variable
};

/**
 * @brief Reads the EGD config from HTTP
 * @param handle Handle to an initialized config object
 * @param producer_id The producer_id to add to the HTTP request
 * @return EgdStatus that will report the status
 */
EgdStatus egd_config_read(EgdConfigHandle* handle, uint32_t producer_id);

/**
 * @brief Gets the config time for this config object
 * @param handle Handle to an EGD config object that will be searched for the config time
 * @param config_time The time since epoch that this object was configured
 * @return EgdStatus that will report the status (always OK for now)
 */
EgdStatus egd_config_get_config_time(const EgdConfigHandle* handle, struct DataTime* config_time);

/**
 * @brief Reads the config that has already been read in to find information on the requested page
 * @param handle Handle to an EGD config object that will be searched for the page info
 * @param page_name The page name to search for
 * @param info On success will be populated with information about the requested page, must not be NULL
 * @warning The info field passed back is only valid until the EgdConfigHandle is freed.
 * @return EgdStatus that will report the status
 */
EgdStatus egd_config_get_page_info(const EgdConfigHandle* handle, const char* page_name, struct EgdPageInfo* info);

/**
 * @brief Looks up information about the requested variable and populates info with the information
 * @param handle The handle to use to lookup the information
 * @param var_name The name of the variable within the page to lookup
 * @param page_info Information on the Page that contains the requested variable
 * @param var_info The handle to populate with the value, must not be NULL
 * @return EgdStatus that will report the status
 */
EgdStatus egd_config_get_var_info(const EgdConfigHandle* handle, const char* var_name,
    const struct EgdPageInfo* page_info, struct EgdVarInfo* var_info);

#ifdef __cplusplus
}
#endif

#endif  // _LIBEGD_EGD_CONFIG_EGD_CONFIG_H_
