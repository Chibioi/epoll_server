package main

import "epoll_project/epoll"

func main() {
	epoll.RunEpollServer()
	epoll.RunClient()
}
