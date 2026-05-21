package epoll

import (
	"errors"
	"fmt"
	"log"
	"net"
	"os"
	"sync"

	"golang.org/x/sys/unix"
)

const (
	max_events  = 128
	listen_port = ":8080"
)

var (
	ConnMap   = make(map[int]net.Conn)
	ConnMapMu sync.Mutex
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

	// get the raw integer descriptor and duplicates the fd so it stays alive after file.Close()
	fd, err := unix.Dup(int(file.Fd()))
	if err != nil {
		return 0, fmt.Errorf("unix.Dup: %w", err)
	}
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
		clientConn, err := FdToNetConn(conn_fd) // COME BACK TO THIS
		if err != nil {
			log.Printf("fdToNetConn: %v", err)
			unix.Close(conn_fd)
			continue
		}
		ConnMapMu.Lock()              // locks mutex
		ConnMap[conn_fd] = clientConn // maps FDs to the its relative connection endpoint
		ConnMapMu.Unlock()            // unlocks mutex

		err = AddToEpoll(epoll_fd, conn_fd)
		if err != nil {
			log.Printf("AddToEpoll(client): %v", err)
			clientConn.Close() // close the connection FD
			ConnMapMu.Lock()
			delete(ConnMap, conn_fd) // delete an element with a valid key in the hash table
			ConnMapMu.Unlock()
			continue
		}
		addr := AddrFromSockaddr(sa)
		fmt.Printf("[+] New connection from %s (fd=%d)\n", addr, conn_fd)

		// Send a greeting immediately.
		unix.Write(conn_fd, []byte("Hello from the epoll server!\n")) //nolint:errcheck
	}
}

func ReadAll(epoll_fd, fd int) {
	// create a byte slice of 4096 bytes
	buf := make([]byte, 4096)
	for {
		n, err := unix.Read(fd, buf)
		if err != nil {
			if errors.Is(err, unix.EAGAIN) || errors.Is(err, unix.EWOULDBLOCK) {
				return // drained
			}
			// Real error or EOF => Close the connection
			log.Printf("Real error on fd=%d: %v - closing", fd, err)
			CloseClient(epoll_fd, fd)
			return
		}
		if n == 0 {
			// EOF: Peer closed the connection.
			fmt.Printf("Connection closed on the FD %d\n", fd)
			CloseClient(epoll_fd, fd)
			return
		}
		fmt.Printf("FD %d received %d bytes: %s\n", fd, n, buf[:n])
		// Echo back to the client
		unix.Write(fd, buf[:n])
	}
}

// CloseClient removes fd from epoll, closes the net.Conn (if any) and cleans up the ConnMap
func CloseClient(epoll_fd, fd int) {
	RemoveFromEpoll(epoll_fd, fd)
	ConnMapMu.Lock()
	conn, ok := ConnMap[fd] // CHECK THIS LATER
	if ok {
		conn.Close()
		delete(ConnMap, fd)
	}
	ConnMapMu.Unlock()
}

// Main event loop
func RunEpollServer() {
	ln, err := net.Listen("tcp", listen_port)
	if err != nil {
		log.Fatalf("Listening error: %v\n", err)
	}
	defer ln.Close()

	// Get listener FD
	listen_fd, err := Get_raw_fd(ln)
	if err != nil {
		log.Fatalf("Get_Raw_Fd: %v\n", err)
	}

	epoll_fd, err := unix.EpollCreate1(0)
	if err != nil {
		log.Fatalf("EpollCreate1: %v\n", err)
	}
	// Close the epoll instance before function goes out of scope
	defer unix.Close(epoll_fd)
	// Add to the Epoll instance
	err = AddToEpoll(epoll_fd, listen_fd)
	if err != nil {
		log.Fatalf("AddToEpoll(listen): %v\n", err)
	}
	fmt.Printf("epoll server listening on %s\n", listen_port)

	// Buffer size for 128 events
	events := make([]unix.EpollEvent, max_events)
	for {
		_, err := unix.EpollWait(epoll_fd, events, -1)
		if err != nil {
			if errors.Is(err, unix.EINTR) {
				// Interrupted by signal - just retry
				continue
			}
			log.Printf("EpollWait error: %v\n", err)
			continue
		}

		for _, ev := range events {
			fd := int(ev.Fd)

			// Handle errors or hang-up events first
			if ev.Events&(unix.EPOLLERR|unix.EPOLLHUP) != 0 {
				log.Printf("EPOLLERR/EPOLLHUP on fd=%d — closing", fd)
				if fd == listen_fd {
					log.Fatal("Error on listening socket — exiting")
				}
				CloseClient(epoll_fd, fd)
				continue
			}

			if fd == listen_fd {
				// accept connection
				AcceptAll(epoll_fd, listen_fd)
			} else {
				// data available on a client FD
				ReadAll(epoll_fd, fd)
			}
		}
	}
}

// Minimal test client
func RunClient() {
	conn, err := net.Dial("tcp", "127.0.0.1:"+listen_port)
	if err != nil {
		log.Fatalf("Dial: %v\n", err)
	}

	defer conn.Close()

	// buffer of size 64
	buf := make([]byte, 64)
	// Read from the connection into the buffer
	n, err := conn.Read(buf)
	if err != nil {
		log.Printf("Read: %v", err)
		return
	}
	fmt.Printf("Message from server: %s\n", buf[:n])

	// Write something back to the echo path
	if _, err := conn.Write([]byte("Ping\n")); err != nil {
		log.Printf("Write: %v", err)
	}
}

// Type assert from fd to NetConn
func FdToNetConn(fd int) (net.Conn, error) {
	f := os.NewFile(uintptr(fd), fmt.Sprintf("tcp-conn-%d", fd))
	if f == nil {
		return nil, fmt.Errorf("os.NewFile returned nil for fd=%d", fd)
	}
	conn, err := net.FileConn(f)
	f.Close() // FileConn dups fd internally; close the wrapper
	if err != nil {
		return nil, fmt.Errorf("net.FileConn: %w", err)
	}
	return conn, nil
}

// Get the address type from the socket address
func AddrFromSockaddr(sa unix.Sockaddr) string {
	switch v := sa.(type) {
	case *unix.SockaddrInet4:
		return fmt.Sprintf("%d.%d.%d.%d:%d", v.Addr[0], v.Addr[1], v.Addr[2], v.Addr[3], v.Port)
	case *unix.SockaddrInet6:
		return fmt.Sprintf("[%v]:%d", v.Addr, v.Port)
	default:
		return "unknown"
	}
}
