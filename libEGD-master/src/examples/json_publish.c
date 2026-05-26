#include <stdio.h>
#include <unistd.h>
#include "egd/config/producer_config.h"
#include "egd/client/json_client.h"

int main() {
  // Fetch the config from the server
  EgdStatus status;
  EgdConfigHandle* producer_config = egd_producer_config_init("127.0.0.1", 7938);  // Replace the host and port with the actual host and port of the server
  if ((status = egd_config_read(producer_config, 123456)) != OK) {  // Replace this number with the producer ID of the actual page you are looking for
    printf("Error reading config(%d): %s\n", status, egd_status_to_string(status));
    return -1;
  }

  // Get the information on the page
  struct EgdPageInfo page_info;
  if ((status = egd_config_get_page_info(producer_config, "my_page_info", &page_info)) != OK) {
    printf("Error getting page info(%d): %s\n", status, egd_status_to_string(status));
    return -1;
  }

  // Subscribe to the page using the page info, also pass some user data to the callback
  EgdJsonClientHandle* client = egd_json_client_init();

  // Filling out JSON data is much easier, this will set the EGD variable "my_variable_name" to 42, assuming that we found it in the config
  const char* message = "{\"my_variable_name\": 42}";

  while (1) {
    // The IP address here is the IP that you will broadcast the message to. Note that EGD is built on top of UDP, so you can publish to a .255 address to publish to a subnet
    // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
    status = egd_json_client_publish(client, "127.0.0.1", 18246, &page_info, message);
    printf("Published EGD data with status(%d): %s\n", status, egd_status_to_string(status));
    sleep(1);
  }

  // We will never get here, but in theory these would need to be called to clean everything up
  egd_json_client_free(client);
  egd_producer_config_free(producer_config);
}
