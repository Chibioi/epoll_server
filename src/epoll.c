#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int create_socket(void) {
  // AF_INET represents the address family for IPV4
  // SOCK_STREAM represents the type of socket connection(connection-oriented)
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket failed");
    exit(1);
  }
  return sockfd;
}

int set_socket_opt(int sockfd) {
  int option = 1;
  // SOL_SOCKET => setting socket level function rather than option-specific to
  // a particular protocol e.g TCP, IP. Allows local socket IP and port to be
  // reused even though a previous connection using that address is terminated
  return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
}

int bind_socket(int sockfd) {
  struct sockaddr_in address;
  address.sin_family = AF_INET; // address family (IPV4)
  address.sin_port =
      htons(8080); // solves the issue of byte ordering in the network
  address.sin_addr.s_addr = INADDR_ANY; // binds to all available interfaces

  int bind_soc = bind(sockfd, (struct sockaddr *)&address, sizeof(address));
  if (bind_soc < 0) {
    perror("binding failed");
    exit(1);
  }

  int flags = fcntl(sockfd, F_GETFL, 0);
  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("Non-block I/O failed");
    exit(1);
  };

  return bind_soc;
}

int listen_socket(int sockfd) {
  // SOMAXCONN is the maximum queue connections you are allowed to listen()
  int res = listen(sockfd, SOMAXCONN);
  if (res < 0) {
    perror("listening failure");
    exit(1);
  }
  return res;
}

int create_epoll_instance(void) {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("epoll_create1");
    exit(1);
  }
  return epoll_fd;
}

int register_socket(int sockfd, int epoll_fd) {
  // events specifies the data that the kernel should save and return when the
  // corresponding file descriptor becomes ready
  struct epoll_event event;
  // EPOLLIN => The associated file is ready for read operations
  event.events = EPOLLIN; // Monitor read events
  event.data.fd = sockfd; // Store the FD for identification later
  int epoll_ctrl_interface = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &event);
  if(epoll_ctrl_interface < 0) {
    perror("epoll ctrl interface");
      exit(1);
  }
  return epoll_ctrl_interface;
}
