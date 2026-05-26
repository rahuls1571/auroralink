#include <unistd.h>
#include <stdio.h>
#include "egd/config/producer_config.h"
#include "egd/client/json_client.h"

// This callback will be called whenever we receive data from the page we subscribe on
void callback(const struct EgdPageInfo* page_info, const struct DataProductionHdr* header, const EgdStatus status, const char* message, void* user_data) {
  // The status is only important if the signature of the page changes, so it will almost always be a success, but if it is a failure, there is no gaurentee that the data is valid
  if (status != OK) {
    printf("Error in subscription(%d): %s", status, egd_status_to_string(status));
    return;
  }

  // I will not decode the JSON here, since that would require a json library.
  // The resulting JSON will be flat, and the keys will be the variable names
  printf("Received message:\n%s\n", message);
}

int main() {
  // Fetch the config from the server
  EgdStatus status;
  EgdConfigHandle* config_handle = egd_producer_config_init("127.0.0.1", 7938);  // Replace the host and port with the actual host and port of the server
  if ((status = egd_config_read(config_handle, 123456)) != OK) {  // Replace this number with the producer ID of the actual page you are looking for
    printf("Unable to read config(%d): %s", status, egd_status_to_string(status));
    return -1;
  }

  // Get the information on the page
  struct EgdPageInfo page_info;
  if ((status = egd_config_get_page_info(config_handle, "my_page_info", &page_info)) != OK) {  // Replace my_page_info with the name of the page you want information on
    printf("Unable to find requested page(%d): %s", status, egd_status_to_string(status));
    return -1;
  }

  // Subscribe to the page using the page info, also pass some user data to the callback
  EgdJsonClientHandle* client_handle = egd_json_client_init();

  // The IP address here, is the IP that the client will "bind" to. 0.0.0.0 is always a safe bet because that will receive any data that is sent to this computer.
  // A specific IP can be used here, but you will only receive data if it was sent to that specific IP or subnet.
  // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
  if ((status = egd_json_client_subscribe(client_handle, "0.0.0.0", 18246, &page_info, callback, (void*)(config_handle))) != OK) {
    printf("Unable to subscribe(%d): %s", status, egd_status_to_string(status));
    return -1;
  }

  // Just wait forever for messages
  while (1) { sleep(10); }

  // We will obviously never get here, but to clean up properly we need to call these functions
  egd_json_client_free(client_handle);
  egd_producer_config_free(config_handle);
}
