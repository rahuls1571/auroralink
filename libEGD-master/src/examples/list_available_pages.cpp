#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <atomic>
#include <iostream>
#include <algorithm>
#include <functional>
#include "egd/config/producer_config.hpp"

using ::egd::config::ProducerConfig;

constexpr auto HOST = "0.0.0.0";
constexpr auto PORT = 18246;
constexpr auto BUF_SIZE = 128 * 1024;

// Declare a function that will eventually be a lambda
static std::function<void(int)> interrupt_handler;

// Declare the proxy interrupt handler that will call the lambda
static void ProxyInterruptHandler(int signal) {
  interrupt_handler(signal);
}

int main(int argc, char** argv) {
  // Read some command line config for the server and producer id to check
  int opt;
  std::string config_host;
  uint16_t config_port;
  uint64_t producer_id;
  while ((opt = getopt(argc, argv, "h:p:i:")) != -1) {
    switch (opt) {
      case 'h':
        config_host = optarg;
        break;
      case 'p':
        config_port = std::stoi(optarg);
        break;
      case 'i':
        producer_id = std::stoull(optarg);
        break;
      default:
        fprintf(stderr, "Usage: %s [-h Config host] [-p Config port] [-i Producer ID]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  // Fetch the config from the server
  EgdStatus status;
  ProducerConfig producer_config(config_host, config_port);
  if ((status = producer_config.Read(producer_id)) != EgdStatus::OK) {
    std::cout << "Error reading config(" << status << "): " << egd_status_to_string(status) << std::endl;
    return -1;
  }
  ::egd::config::EgdPageMap page_map;
  producer_config.GetPageMap(&page_map);

  // Set up a socket to listen to the data
  // Create the subscribe socket
  SOCKET_TYPE sock;
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
    fprintf(stderr, "Unable to create socket\n");
    exit(EXIT_FAILURE);
  }

  // Allow other applications to bind on this socket
  int should_reuse_addr = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&should_reuse_addr), sizeof(should_reuse_addr))) {
    fprintf(stderr, "Unable to set reuse address\n");
    exit(EXIT_FAILURE);
  }

  // Bind on the subscribe socket
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(HOST);
  addr.sin_port = reinterpret_cast<uint16_t>(htons(PORT));
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    fprintf(stderr, "Unable to bind on socket\n");
    exit(EXIT_FAILURE);
  }

  // Set the socket buf size
  int buf_size = BUF_SIZE;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&buf_size), sizeof(buf_size))) {
    fprintf(stderr, "Unable to set the buffer size on the socket\n");
    exit(EXIT_FAILURE);
  }

  // Register stop handler
  std::atomic<bool> running{true};
  interrupt_handler = [&](int signal) {
    std::cout << "Stopping application" << std::endl;
    close(sock);
    running = false;
  };
  signal(SIGINT, ProxyInterruptHandler);

  // Listen for data, and every time we receive a packet, print that we received it
  std::vector<uint32_t> printed_exchange_ids;
  std::cout << "Listening for available pages for Producer ID: " << producer_id << std::endl;
  while (running) {
    socklen_t from_addr_len;
    sockaddr_in from_addr;
    DataProductionMessage message;
    const auto length = recvfrom(sock, &message, sizeof(message), 0, reinterpret_cast<sockaddr*>(&from_addr), &from_addr_len);
    if (length > 0) {
      const uint32_t exchange_id = message.Hdr.ExchangeID;
      if (std::find(printed_exchange_ids.begin(), printed_exchange_ids.end(), exchange_id) == printed_exchange_ids.end()) {
        for (const auto& page_entry : page_map) {
          const std::string& page_name = page_entry.first;
          const EgdPageInfo& entry = page_entry.second.page;
          if (exchange_id == entry.exchange_id) {
            const size_t from_host_maxlen = 64;
            char from_host[from_host_maxlen];
            inet_ntop(AF_INET, &from_addr.sin_addr, from_host, from_host_maxlen);
            std::cout << "  Received page " << page_name << " from " << from_host << ":" << ntohs(from_addr.sin_port) << std::endl;
            printed_exchange_ids.push_back(exchange_id);
          }
        }
      }
    }
  }
}
