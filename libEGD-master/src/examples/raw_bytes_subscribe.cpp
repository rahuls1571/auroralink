#include <unistd.h>
#include <iostream>
#include "egd/config/producer_config.hpp"
#include "egd/client/egd_client.hpp"

using ::egd::config::ProducerConfig;
using ::egd::client::EgdClient;

// This callback will be called whenever we receive data from the page we subscribe on
void callback(const struct EgdPageInfo* page_info, const DataProductionHdr* header, const EgdStatus status, const uint8_t* data, const size_t data_len, void* user_data) {
  // The status is only important if the signature of the page changes, so it will almost always be a success, but if it is a failure, there is no gaurentee that the data is valid
  if (status != EgdStatus::OK) {
    std::cout << "Error in subscription(" << status << "): " << egd_status_to_string(status) << std::endl;
    return;
  }

  // The data is still a raw payload, so we will use the passed in config object to pull information on the variables we care about
  EgdVarInfo my_var_info;
  ProducerConfig* producer_config = reinterpret_cast<ProducerConfig*>(user_data);
  producer_config->GetVarInfo("my_variable_name", *page_info, &my_var_info);
  const size_t my_var_offset = my_var_info.bit_offset / 8;

  // Decoding variables out of an EGD packet kinda sucks, but let's assume that our type is a 64 bit unsigned integer for now.
  // If this was an actual application, you would want a switch statement in here that determines the type of the variable using "my_var_info.type"
  switch (my_var_info.data_type.type) {
    // We will assume that this will always be a unsigned 64 bit integer, but in production you would want to fill out this entire switch
    case EGD_ULINT: {
      const uint64_t my_var = *reinterpret_cast<const uint64_t*>(data + my_var_offset);
      std::cout << "My Variable is " << my_var << std::endl;
      break;
    }
    case EGD_BOOL:
    case EGD_REAL:
    case EGD_LREAL:
    case EGD_SINT:
    case EGD_INT:
    case EGD_DINT:
    case EGD_LINT:
    case EGD_USINT:
    case EGD_UINT:
    case EGD_UDINT:
    case EGD_BYTE:
    case EGD_WORD:
    case EGD_DWORD:
    case EGD_DT:
    case EGD_TIME:
    case EGD_STRING:
    default:
      break;
  }
}

int main() {
  // Fetch the config from the server
  EgdStatus status;
  ProducerConfig producer_config("127.0.0.1", 7938);  // Replace the host and port with the actual host and port of the server
  if ((status = producer_config.Read(123456)) != EgdStatus::OK) {  // Replace this number with the producer ID of the actual page you are looking for
    std::cout << "Error reading config(" << status << "): " << egd_status_to_string(status) << std::endl;
    return -1;
  }

  // Get the information on the page
  EgdPageInfo page_info;
  if ((status = producer_config.GetPageInfo("my_page_info", &page_info)) != EgdStatus::OK) {
    std::cout << "Error getting page info(" << status << "): " << egd_status_to_string(status) << std::endl;
    return -1;
  }

  // Subscribe to the page using the page info, also pass some user data to the callback
  EgdClient client;

  // The IP address here, is the IP that the client will "bind" to. 0.0.0.0 is always a safe bet because that will receive any data that is sent to this computer.
  // A specific IP can be used here, but you will only receive data if it was sent to that specific IP or subnet.
  // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
  if ((status = client.Subscribe("0.0.0.0", 18246, page_info, callback, reinterpret_cast<void*>(&producer_config))) != EgdStatus::OK) {
    std::cout << "Error subscribing(" << status << "): " << egd_status_to_string(status) << std::endl;
    return -1;
  }

  std::cout << "Subscribed to EGD data, waiting forever" << std::endl;
  while (true) { sleep(10); }
}
