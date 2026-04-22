#ifndef EPOLL_H
#define EPOLL_H

int create_socket(void);
int set_socket_opt(int sockfd);
int setnonblocking(int sockfd); // need test
int bind_socket(int sockfd, int port);
int listen_socket(int sockfd);
int create_epoll_instance(void);
void do_use_fd(int fd, int epoll_fd);   // need test
void do_write_fd(int fd, int epoll_fd); // need test
int register_socket(int sockfd, int epoll_fd);

#endif
