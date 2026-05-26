# Copyright (c) 2019 by General Electric Company. All rights reserved. ***
# The copyright to the computer software herein is the property of
# General Electric Company. The software may be used and/or copied only
# with the written permission of General Electric Company or in accordance
# with the terms and conditions stipulated in the agreement/contract
# under which the software has been supplied.

# Define only project wide things here, everything else should be in the specific cmake files for the project
cmake_minimum_required(VERSION 3.8)
project(EGD)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")

# These variables will be built into the resulting library as metadata
set(DESCRIPTION "Library for reading and writing EGD data over the network")

# Release build flags
set(RELEASE_FLAGS "-s -D_FORTIFY_SOURCE=2 -pie -fpie -Wl,-z,relro,-z,now -fstack-protector-all -fcf-protection=full -mshstk")
set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} ${RELEASE_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(EGD_DIR ${SRC_DIR}/egd)
set(EXAMPLES_DIR ${SRC_DIR}/examples)
set(CLIENT_DIR ${EGD_DIR}/client)
set(CONFIG_DIR ${EGD_DIR}/config)
set(UTIL_DIR ${EGD_DIR}/util)

# Add the source directory as the only include path that we can use for our code
include_directories(${SRC_DIR})

# Name everything we generate
set(EXE_NAME ${PROJECT_NAME}Example)
set(LIB_NAME ${PROJECT_NAME})

# Build the plugin library
set(LIB_SRC_FILES
        ${CLIENT_DIR}/egd_client.cpp
        ${CLIENT_DIR}/egd_client.hpp
        ${CLIENT_DIR}/egd_client.h
        ${CLIENT_DIR}/json_client.cpp
        ${CLIENT_DIR}/json_client.hpp
        ${CLIENT_DIR}/json_client.h
        ${CONFIG_DIR}/consumer_config.cpp
        ${CONFIG_DIR}/consumer_config.hpp
        ${CONFIG_DIR}/consumer_config.h
        ${CONFIG_DIR}/producer_config.cpp
        ${CONFIG_DIR}/producer_config.hpp
        ${CONFIG_DIR}/producer_config.h
        ${CONFIG_DIR}/egd_config.cpp
        ${CONFIG_DIR}/egd_config.hpp
        ${CONFIG_DIR}/egd_config.h
        ${UTIL_DIR}/egd_spec.h
        ${UTIL_DIR}/egd_status.cpp
        ${UTIL_DIR}/egd_status.h
        ${UTIL_DIR}/external/CxxUrl/url.cpp
        ${UTIL_DIR}/external/CxxUrl/url.hpp
        ${UTIL_DIR}/external/CxxUrl/string.hpp
        ${UTIL_DIR}/external/json/json.hpp
        ${UTIL_DIR}/external/pugixml/pugixml.cc
        ${UTIL_DIR}/external/pugixml/pugixml.hpp
        ${UTIL_DIR}/external/pugixml/pugiconfig.hpp
        )


add_library(${LIB_NAME}-static STATIC ${LIB_SRC_FILES})
set_property(TARGET ${LIB_NAME}-static PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(${LIB_NAME}-static PROPERTIES OUTPUT_NAME ${LIB_NAME})
target_link_libraries(${LIB_NAME}-static ${STATIC_DEPS})

set(STATIC_DEPS /usr/local/lib/libcurl.a /usr/local/lib/libnghttp2.a /usr/local/lib/libpsl.a)
add_library(${LIB_NAME}-shared SHARED ${LIB_SRC_FILES})
set_property(TARGET ${LIB_NAME}-shared PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(${LIB_NAME}-shared PROPERTIES OUTPUT_NAME ${LIB_NAME})
target_link_libraries(${LIB_NAME}-shared ${STATIC_DEPS} pthread ssl z)

# Compile some examples
add_executable(JsonPublishCPP ${EXAMPLES_DIR}/json_publish.cpp)
target_link_libraries(JsonPublishCPP ${LIB_NAME}-shared pthread ssl z)
add_executable(JsonPublishC ${EXAMPLES_DIR}/json_publish.c)
target_link_libraries(JsonPublishC ${LIB_NAME}-shared pthread ssl z)
add_executable(JsonSubscribeCPP ${EXAMPLES_DIR}/json_subscribe.cpp)
target_link_libraries(JsonSubscribeCPP ${LIB_NAME}-shared pthread ssl z)
add_executable(JsonSubscribeC ${EXAMPLES_DIR}/json_subscribe.c)
target_link_libraries(JsonSubscribeC ${LIB_NAME}-shared pthread ssl z)
add_executable(RawBytesPublishCPP ${EXAMPLES_DIR}/raw_bytes_publish.cpp)
target_link_libraries(RawBytesPublishCPP ${LIB_NAME}-shared pthread ssl z)
add_executable(RawBytesPublishC ${EXAMPLES_DIR}/raw_bytes_publish.c)
target_link_libraries(RawBytesPublishC ${LIB_NAME}-shared pthread ssl z)
add_executable(RawBytesSubscribeCPP ${EXAMPLES_DIR}/raw_bytes_subscribe.cpp)
target_link_libraries(RawBytesSubscribeCPP ${LIB_NAME}-shared pthread ssl z)
add_executable(RawBytesSubscribeC ${EXAMPLES_DIR}/raw_bytes_subscribe.c)
target_link_libraries(RawBytesSubscribeC ${LIB_NAME}-shared pthread ssl z)
add_executable(ListAvailablePlugins ${EXAMPLES_DIR}/list_available_pages.cpp)
target_link_libraries(ListAvailablePlugins ${LIB_NAME}-shared pthread ssl z)

# Install the headers and libraries
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/egd
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
        )
install(TARGETS ${LIB_NAME}-shared ${LIB_NAME}-static ListAvailablePlugins
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        )
