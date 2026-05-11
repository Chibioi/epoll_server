package main

import (
	"bufio"
	"errors"
	"fmt"
	"log"
	"net"
	"sync"

	"golang.org/x/sys/unix"
)

const (
	max_events  = 128
	listen_port = ":8080"
)

var (
	connMap   = make(map[int]net.Conn)
	connMapMu sync.Mutex
)

// we want to extract the file descriptor using this function
func Get_raw_fd(Listen net.Listener) (int, error) {
	tcpListen, ok := Listen.(*net.TCPListener) // type asserting from net.Conn to net.TCPConn
	if !ok {
		return 0, fmt.Errorf("listener is not a TCP listener")
	}
	// this duplicates the FDs
	file, err := tcpListen.File()
	if err != nil {
		return 0, fmt.Errorf("TCPListener.File: %w", err)
	}
	defer file.Close() // close wrapper to prevent FD leak

	// get the raw integer descriptor
	fd := int(file.Fd())
	// set it to non-blocking to use for epoll
	err = unix.SetNonblock(fd, true)
	if err != nil {
		unix.Close(fd) // we are using unix.Close() because this closes the original FD and not the duplicate
		return 0, fmt.Errorf("SetNonblock: %w", err)
	}
	return fd, nil
}

// AddToEpoll() registers FDs to edge-triggered read events
func AddToEpoll(epoll_fd, fd int) error {
	ev := unix.EpollEvent{
		Events: unix.EPOLLIN | unix.EPOLLET, // edge-triggered read
		Fd:     int32(fd),
	}
	return unix.EpollCtl(epoll_fd, unix.EPOLL_CTL_ADD, fd, &ev)
}

// RemoveFromEpoll() deregisters FDs and closes it
func RemoveFromEpoll(epoll_fd, fd int) {
	unix.EpollCtl(epoll_fd, unix.EPOLL_CTL_DEL, fd, nil) //nolint:errcheck
	unix.Close(fd)
}

// This drains all pending connections on the listening FD
func AcceptAll(epoll_fd, listen_fd int) {
	for {
		// Accept connections at the syscall level so that we stay on the raw FD
		conn_fd, sa, err := unix.Accept(listen_fd)
		if err != nil {
			if errors.Is(err, unix.EAGAIN) || errors.Is(err, unix.EWOULDBLOCK) {
				// drained — stop looping (EPOLLET requirement)
				return
			}
			log.Printf("Accept error: %v", err)
			return
		}
		// Make the client FD non-blocking too.
		err = unix.SetNonblock(conn_fd, true)
		if err != nil {
			log.Printf("SetNonblock(connFd): %v", err)
			unix.Close(conn_fd)
			continue
		}
		// Reconstruct a net.Conn from the raw FD so we can use Go's
		// higher-level I/O helpers (bufio, RemoteAddr, etc.).
		clientConn, err := fdToNetConn(conn_fd) // COME BACK TO THIS
		if err != nil {
			log.Printf("fdToNetConn: %v", err)
			unix.Close(conn_fd)
			continue
		}
		connMapMu.Lock()              // locks mutex
		connMap[conn_fd] = clientConn // maps FDs to the its relative connection endpoint
		connMapMu.Unlock()            // unlocks mutex

		err = AddToEpoll(epoll_fd, conn_fd)
		if err != nil {
			log.Printf("AddToEpoll(client): %v", err)
			clientConn.Close() // close the connection FD
			connMapMu.Lock()
			delete(connMap, conn_fd) // delete an element with a valid key in the hash table
			connMapMu.Unlock()
			continue
		}
		addr := addrFromSockaddr(sa)
		fmt.Printf("[+] New connection from %s (fd=%d)\n", addr, conn_fd)

		// Send a greeting immediately.
		unix.Write(conn_fd, []byte("Hello from the epoll server!\n")) //nolint:errcheck
	}
}

func Epoll_instance() {
	// creating an epoll instance
	epoll_fd, err := unix.EpollCreate1(0)
	if err != nil {
		panic(err)
	}
	defer unix.Close(epoll_fd)

	// prepare a listening socket
	ln, err := net.Listen("tcp", ":8080")
	if err != nil {
		panic(err)
	}

	// get the FD of the listening socket
	fd, err := Get_raw_fd(ln)
	if err != nil {
		panic(err)
	}

	// Register FD with Epoll
	// We listen for EPOLLIN (available to read) and use Edge-Triggered (EPOLLET) mode
	ev := &unix.EpollEvent{
		Events: unix.EPOLLIN | unix.EPOLLET, // readiness for incoming connections and of edge triggered type
		Fd:     int32(fd),
	}

	// Add an event to the epoll
	err = unix.EpollCtl(epoll_fd, unix.EPOLL_CTL_ADD, fd, ev)
	if err != nil {
		panic(err)
	}
	// The event loop
	events := make([]unix.EpollEvent, 10)
	for {
		// wait for events (blocks until something happen)
		no_of_events, err := unix.EpollWait(epoll_fd, events, -1)
		if err != nil {
			continue
		}
		for i := 0; i < no_of_events; i++ {
			if int(events[i].Fd) == fd {
				fmt.Println("New connection to the server socket")
				conn_fd, _, err := unix.Accept(fd)
				if err != nil {
					if errors.Is(err, unix.EAGAIN) || errors.Is(err, unix.EWOULDBLOCK) {
						break // drained, stop looping
					}
					fmt.Println("Accept error:", err)
					break
				}
			}
		}
	}

}
