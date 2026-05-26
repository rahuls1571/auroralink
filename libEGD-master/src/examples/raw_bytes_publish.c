#include <stdio.h>
#include <unistd.h>
#include "egd/config/producer_config.h"
#include "egd/client/egd_client.h"

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
  EgdClientHandle* client = egd_client_init();

  // For the sake of example we will just set a single variable, so grab the information about it here
  struct EgdVarInfo my_var_info;
  if ((status = egd_config_get_var_info(producer_config, "my_variable_name", &page_info, &my_var_info)) != OK) {
    printf("Error getting variable info(%d): %s\n", status, egd_status_to_string(status));
    return -1;
  }
  const size_t my_var_offset = my_var_info.bit_offset / 8;

  // Filling out raw data is a bit of a pain, since you need to manually go to the byte offset specified in the configuration, then insert the proper number of bytes
  uint8_t data[MAX_EGD_PAYLOAD];
  switch (my_var_info.data_type.type) {
    // We will assume that this will always be a unsigned 64 bit integer, but in production you would want to fill out this entire switch
    case EGD_ULINT:
      data[my_var_offset] = 42;
      break;
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

  while (1) {
    // The IP address here is the IP that you will broadcast the message to. Note that EGD is built on top of UDP, so you can publish to a .255 address to publish to a subnet
    // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
    status = egd_client_publish(client, "127.0.0.1", 18246, &page_info, data, MAX_EGD_PAYLOAD);
    printf("Published EGD data with status(%d): %s\n", status, egd_status_to_string(status));
    sleep(1);
  }

  // We will never get here, but in theory these would need to be called to clean everything up
  egd_client_free(client);
  egd_producer_config_free(producer_config);
}
