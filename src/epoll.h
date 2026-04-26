#ifndef EPOLL_H
#define EPOLL_H

#include <unistd.h>
#define MAX_EVENTS 64
#define BUF_SIZE 1024
#define MAX_CLIENTS 1024

// struct sockaddr_in address;
// struct epoll_event event[MAX_EVENTS];

// user defined data type for my write buffer
typedef struct {
  char buf[BUF_SIZE];
  size_t len;
} write_buf_t;

extern write_buf_t write_buffers[MAX_CLIENTS];

int create_socket(void);
int set_socket_opt(int sockfd);
int setnonblocking(int sockfd); // need test
int bind_socket(int sockfd);
int listen_socket(int sockfd);
int create_epoll_instance(void);
void do_use_fd(int fd, int epoll_fd);   // need test
void do_write_fd(int fd, int epoll_fd); // need test
void register_socket(int epoll_fd);

#endif
