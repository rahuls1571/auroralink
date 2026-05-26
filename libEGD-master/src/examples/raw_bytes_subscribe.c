#include <unistd.h>
#include <stdio.h>
#include "egd/config/producer_config.h"
#include "egd/client/egd_client.h"

// This callback will be called whenever we receive data from the page we subscribe on
void callback(const struct EgdPageInfo* page_info, const struct DataProductionHdr* header, const EgdStatus status, const uint8_t* data, const size_t data_len, void* user_data) {
  // The status is only important if the signature of the page changes, so it will almost always be a success, but if it is a failure, there is no gaurentee that the data is valid
  if (status != OK) {
    printf("Error in subscription(%d): %s", status, egd_status_to_string(status));
    return;
  }

  // The data is still a raw payload, so we will use the passed in config object to pull information on the variables we care about
  struct EgdVarInfo my_var_info;
  EgdConfigHandle* producer_config = (EgdConfigHandle*)(user_data);
  egd_config_get_var_info(producer_config, "my_variable_name", page_info, &my_var_info);
  const size_t my_var_offset = my_var_info.bit_offset / 8;

  // Decoding variables out of an EGD packet kinda sucks, but let's assume that our type is a 64 bit unsigned integer for now.
  // If this was an actual application, you would want a switch statement in here that determines the type of the variable using "my_var_info.type"
  switch (my_var_info.data_type.type) {
    // We will assume that this will always be a unsigned 64 bit integer, but in production you would want to fill out this entire switch
    case EGD_ULINT: {
      const uint64_t my_var = *(const uint64_t*)(data + my_var_offset);
      printf("My Variable is %llu\n", my_var);
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
  EgdClientHandle* client_handle = egd_client_init();

  // The IP address here, is the IP that the client will "bind" to. 0.0.0.0 is always a safe bet because that will receive any data that is sent to this computer.
  // A specific IP can be used here, but you will only receive data if it was sent to that specific IP or subnet.
  // The port here should pretty much always be 18246 if you are trying to communicate with anything adhering to the EGD spec, but can be  changed if you control both the publisher a nd subscriber
  if ((status = egd_client_subscribe(client_handle, "0.0.0.0", 18246, &page_info, callback, (void*)(config_handle))) != OK) {
    printf("Unable to subscribe(%d): %s", status, egd_status_to_string(status));
    return -1;
  }

  // Just wait forever for messages
  while (1) { sleep(10); }

  // We will obviously never get here, but to clean up properly we need to call these functions
  egd_client_free(client_handle);
  egd_producer_config_free(config_handle);
}
