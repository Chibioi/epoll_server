#include "epoll.h"
#include <stdio.h>

int main(void) {
  int epoll_fd = create_epoll_instance();
  puts("epoll echo server listening on port 8080...");
  register_socket(epoll_fd);
  return 0;
}
