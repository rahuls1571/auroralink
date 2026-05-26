#include <unistd.h>
#include <iostream>
#include "egd/config/producer_config.hpp"
#include "egd/client/json_client.hpp"

using ::egd::config::ProducerConfig;
using ::egd::client::JsonClient;

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

  // Filling out JSON data is much easier, this will set the EGD variable "my_variable_name" to 42, assuming that we found it in the config
  const std::string message = R"({"my_variable_name": 42})";

  while (true) {
    // The IP address here is the IP that you will broadcast the message to. Note that EGD is built on top of UDP, so you can publish to a .255 address to publish to a subnet
    // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
    status = client.Publish("127.0.0.1", 18246, page_info, message);
    std::cout << "Published EGD data with status(" << status << "): " << egd_status_to_string(status) << std::endl;
    sleep(1);
  }
}
