package main

import (
	"fmt"
	"golang.org/x/sys/unix"
	"unsafe"
)

// declare the write_buf_t struct
type Write_buf_t struct {
	// header uint16
	data [1024]byte // telling the CPU to create a space of 1024 bytes in memory
	len  int        // length of the buffer
}

// Create the socket interface
func Create_socket() int {
	fd, err := unix.Socket(unix.AF_INET, unix.SOCK_STREAM, 0)
	if err != nil {
		panic(err)
	}
	return fd
}

// set socket options
func Set_socket_opt(sockfd int) {
	var option int = 1
	err := unix.SetsockoptInt(sockfd, unix.SOL_SOCKET, unix.SO_REUSEADDR, option)
	if err != nil {
		panic(err)
	}
}

func Bind_listensocket(sockfd int) {
	address := &unix.SockaddrInet4{
		// Port number
		Port: 8080,
		// signifies IPV4 address
		Addr: [4]byte{0, 0, 0, 0}, // INADDR_ANY
	}

	// bind the socket
	err := unix.Bind(sockfd, address)
	if err != nil {
		panic(err)
	}

	// listens for new connections
	lis_err := unix.Listen(sockfd, 10)
	if lis_err != nil {
		panic(err)
	}
}

func epoll_instance(epoll_fd int) {
	// creating an epoll instance
	epoll_fd, err := unix.EpollCreate1(0)
	if err != nil {
		panic(err)
	}

	// creating an epoll event
	var event [10]unix.EpollEvent
	sockfd := Create_socket()
	sockfd_32 := int32(sockfd) // integer overfolw
	Set_socket_opt(sockfd)
	Bind_listensocket(sockfd)

	// Register listening socket with epoll
	ev := &unix.EpollEvent{
		Events: unix.EPOLLIN | unix.EPOLLET,
		Fd:     sockfd_32,
	}
}

// FIND OUT HOW TO MITIGATE THE INTEGER OVERFLOW IN THE EPOLL EVENT STRUCT
