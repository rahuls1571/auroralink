#include <unistd.h>
#include <iostream>
#include "egd/config/producer_config.hpp"
#include "egd/client/json_client.hpp"

using ::egd::config::ProducerConfig;
using ::egd::client::JsonClient;

// This callback will be called whenever we receive data from the page we subscribe on
void callback(const struct EgdPageInfo* page_info, const DataProductionHdr* header, const EgdStatus status, const char* message, void* user_data) {
  // The status is only important if the signature of the page changes, so it will almost always be a success, but if it is a failure, there is no gaurentee that the data is valid
  if (status != EgdStatus::OK) {
    return;
  }

  // I will not decode the JSON here, since that would require a json library.
  // The resulting JSON will be flat, and the keys will be the variable names
  std::cout << "Received message:\n" << message << std::endl;
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
  JsonClient client;

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
