#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define BUF_SIZE 1024
#define MAX_CLIENTS 1024

struct sockaddr_in address;
struct epoll_event event[MAX_EVENTS];
socklen_t addresslen = sizeof(address);

typedef struct {
  char buf[BUF_SIZE];
  size_t len;
} write_buf_t;

write_buf_t write_buffers[MAX_CLIENTS];

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

int setnonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags < 0) {
    perror("Non-block I/O failed");
    exit(1);
  };
  return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int bind_socket(int sockfd) {
  address.sin_family = AF_INET; // address family (IPV4)
  address.sin_port =
      htons(8080); // solves the issue of byte ordering in the network
  address.sin_addr.s_addr = INADDR_ANY; // binds to all available interfaces

  int bind_soc = bind(sockfd, (struct sockaddr *)&address, sizeof(address));
  if (bind_soc < 0) {
    perror("binding failed");
    exit(1);
  }
  setnonblocking(sockfd);
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

void do_use_fd(int fd, int epoll_fd) {
  struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data.fd = fd};
  char buffer[BUF_SIZE];
  while (1) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("read error");
        close(fd); // clean up on error
      }
      break; // No more data to read for now
    } else if (count == 0) {
      printf("client disconnected on FD %d\n", fd);
      close(fd);
      break;
    }
    printf("Read %zd bytes", count);

    size_t space = BUF_SIZE - write_buffers[fd].len;
    size_t to_copy = (size_t)count < space ? (size_t)count : space;
    // Queue a response into the write buffer
    memcpy(write_buffers[fd].buf + write_buffers[fd].len, buffer, to_copy);
    write_buffers[fd].len += to_copy;

    // Arm EPOLLOUT so we get notified when the socket is writable
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
  }
}

void do_write_fd(int fd, int epoll_fd) {
  struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data.fd = fd};
  write_buf_t *wb = &write_buffers[fd];
  size_t written = 0;

  while (written < wb->len) {
    ssize_t n = write(fd, wb->buf + written, wb->len - written);
    if (n == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break; // socket full, retry later
      perror("write error");
      close(fd);
      return;
    }
    written += n;
  }

  // Shift out the bytes we wrote
  wb->len -= written;
  if (wb->len > 0)
    memmove(wb->buf, wb->buf + written, wb->len); // more data remains

  // If buffer is fully drained, disarm EPOLLOUT to avoid busy-looping
  if (wb->len == 0) {
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data.fd = fd};
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
  }
}

void register_socket(int epoll_fd) {
  // events specifies the data that the kernel should save and return when the
  // corresponding file descriptor becomes ready
  // EPOLLIN => The associated file is ready for read operations

  int sockfd = create_socket();
  set_socket_opt(sockfd);
  bind_socket(sockfd);
  listen_socket(sockfd);
  struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data.fd = sockfd};
  int epoll_ctrl_interface = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
  if (epoll_ctrl_interface < 0) {
    perror("epoll ctrl interface");
    exit(1);
  }

  while (1) {
    int nfds = epoll_wait(epoll_fd, event, MAX_EVENTS, -1);
    if (nfds < 0) {
      perror("nfds");
      exit(1);
    }
    for (int i = 0; i < nfds; i++) {
      if (event[i].data.fd == sockfd) {
        // Handle connection
        while (1) {
          int conn_sock =
              accept(sockfd, (struct sockaddr *)&address, &addresslen);
          if (conn_sock == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            perror("accept");
            exit(1);
          }
          setnonblocking(conn_sock);
          struct epoll_event conn_ev = {.events = EPOLLIN | EPOLLET,
                                        .data.fd = conn_sock};
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
            perror("epoll_ctl: conn_sock");
            exit(1);
          }
        }
      } else {
        if (event[i].events & EPOLLIN) {
          do_use_fd(event[i].data.fd, epoll_fd);
        }
        if (event[i].events & EPOLLOUT) {
          do_write_fd(event[i].data.fd, epoll_fd);
        }
      }
    }
  }
}
