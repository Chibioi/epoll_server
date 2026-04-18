#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
  // AF_INET represents the address family for IPV4
  // SOCK_STREAM represents the type of socket connection(connection-oriented)
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket failed");
    exit(1);
  }

  int option = 1;
  // SOL_SOCKET => setting socket level function rather than option-specific to
  // a particular protocol e.g TCP, IP. Allows local socket IP and port to be
  // reused even though a previous connection using that address is terminated
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

  struct sockaddr_in address;
  address.sin_family = AF_INET; // address family (IPV4)
  address.sin_port =
      htons(8080); // solves the issue of byte ordering in the network
  address.sin_addr.s_addr = INADDR_ANY; // binds to all available interfaces

  if (bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("binding failed");
    exit(1);
  }

  int flags = fcntl(sockfd, F_GETFL, 0);
  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("Non-block I/O failed");
    exit(1);
  };

  // SOMAXCONN is the maximum queue connections you are allowed to listen()
  if (listen(sockfd, SOMAXCONN) < 0) {
    perror("listening failure");
    exit(1);
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("epoll_create1");
    exit(1);
  }
  return EXIT_SUCCESS;
}
