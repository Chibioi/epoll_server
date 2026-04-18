#ifndef EPOLL_H
#define EPOLL_H

int create_socket(void);
int set_socket_opt(int sockfd);
int bind_socket(int sockfd, int port);
int listen_socket(int sockfd);
int create_epoll_instance(void);

#endif
