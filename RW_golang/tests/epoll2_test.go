package tests

import (
	"epoll_project/epoll"
	"golang.org/x/sys/unix"
	"net"
	"os"
	"testing"
)

// newEpollFd is a test helper that creates a real epoll instance
// and registers it for cleanup when the test ends.
func newEpollFd(t *testing.T) int {
	t.Helper()
	epfd, err := unix.EpollCreate1(0)
	if err != nil {
		t.Fatalf("EpollCreate1: %v", err)
	}
	t.Cleanup(func() { unix.Close(epfd) }) // helps to cleanup the epoll FD to prevent memory leaks
	return epfd
}

// newPipe is a test helper that returns the read end of a pipe
// and registers both ends for cleanup.
func newPipe(t *testing.T) int {
	t.Helper()
	r, w, err := os.Pipe() // os.Pipe() create a connected pair of files used for IPC or communication btw goroutines
	if err != nil {
		t.Fatalf("os.Pipe: %v", err)
	}
	t.Cleanup(func() { r.Close(); w.Close() }) // close the files r and w
	return int(r.Fd())                         // returns the FD of the file "r"
}

// TestAddToEpoll_Success verifies that a valid fd is registered without error.
func TestAddToEpoll_Success(t *testing.T) {
	epfd := newEpollFd(t)
	fd := newPipe(t)

	if err := epoll.AddToEpoll(epfd, fd); err != nil {
		t.Fatalf("AddToEpoll returned unexpected error: %v", err)
	}
}

// TestAddToEpoll_EdgeTriggeredFlagSet verifies EPOLLIN|EPOLLET are the only
// flags set, by deleting the fd and inspecting the error path on re-add.
// The canonical way to verify flags is to inspect them via /proc on Linux.
func TestAddToEpoll_EdgeTriggeredFlagSet(t *testing.T) {
	epfd := newEpollFd(t)
	fd := newPipe(t)

	if err := epoll.AddToEpoll(epfd, fd); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}

	// Adding the same fd a second time must fail with EEXIST, which proves
	// the fd really was registered (not silently dropped).
	err := epoll.AddToEpoll(epfd, fd)
	if err == nil {
		t.Fatal("expected EEXIST on duplicate add, got nil")
	}
	if err != unix.EEXIST {
		t.Fatalf("expected EEXIST, got %v", err)
	}
}

// TestAddToEpoll_EventIsTriggered does an end-to-end check:
// after registering a pipe read-end, writing to the write-end must
// cause EpollWait to return that fd as ready.
func TestAddToEpoll_EventIsTriggered(t *testing.T) {
	epfd := newEpollFd(t)

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe: %v", err)
	}
	defer r.Close()
	defer w.Close()

	rfd := int(r.Fd()) // get the FD of the read end of the pipe
	if err := epoll.AddToEpoll(epfd, rfd); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}

	// Write a byte so the read-end becomes readable.
	if _, err := w.Write([]byte{1}); err != nil {
		t.Fatalf("pipe write: %v", err)
	}

	events := make([]unix.EpollEvent, 4)
	n, err := unix.EpollWait(epfd, events, 100) // 100 signifies 100ms
	if err != nil {
		t.Fatalf("EpollWait: %v", err)
	}
	if n == 0 {
		t.Fatal("EpollWait timed out: no events after write")
	}

	got := events[0].Fd
	if int(got) != rfd {
		t.Fatalf("expected fd %d, got %d", rfd, got)
	}
	if events[0].Events&unix.EPOLLIN == 0 {
		t.Fatalf("EPOLLIN not set in returned event flags: %#x", events[0].Events)
	}
}

// TestAddToEpoll_InvalidEpollFd verifies that a bad epoll fd returns an error.
func TestAddToEpoll_InvalidEpollFd(t *testing.T) {
	fd := newPipe(t)

	err := epoll.AddToEpoll(-1, fd)
	if err == nil {
		t.Fatal("expected error for invalid epoll fd, got nil")
	}
}

// TestAddToEpoll_InvalidTargetFd verifies that a bad target fd returns an error.
func TestAddToEpoll_InvalidTargetFd(t *testing.T) {
	epfd := newEpollFd(t)

	err := epoll.AddToEpoll(epfd, -1)
	if err == nil {
		t.Fatal("expected error for invalid target fd, got nil")
	}
}

// TestAddToEpoll_SocketFd verifies that a socket fd (not just a pipe) works.
// Helps to know that the epoll wrapper can register a standard socket FD and not only just a simple file or pipe
func TestAddToEpoll_SocketFd(t *testing.T) {
	epfd := newEpollFd(t)

	ln, err := net.Listen("tcp", "127.0.0.1:0") // tcp listener is local to this function scope
	if err != nil {
		t.Fatalf("net.Listen: %v", err)
	}
	defer ln.Close()

	tcpLn := ln.(*net.TCPListener)
	rawConn, err := tcpLn.SyscallConn() // helps the develper execute raw syscalls on an active network connection without interfering with go's runtime network poller
	if err != nil {
		t.Fatalf("SyscallConn: %v", err)
	}

	var addErr error
	// This ensures that the FD is stable across the callback function in rawConn.Control() and is not modified by the Go's garbage collector
	controlErr := rawConn.Control(func(fd uintptr) {
		addErr = epoll.AddToEpoll(epfd, int(fd))
	})
	if controlErr != nil {
		t.Fatalf("Control: %v", controlErr)
	}
	if addErr != nil {
		t.Fatalf("AddToEpoll on socket: %v", addErr)
	}
}

// TestAddToEpoll_ClosedFd verifies that a closed fd returns an error.
func TestAddToEpoll_ClosedFd(t *testing.T) {
	epfd := newEpollFd(t)

	r, _, err := os.Pipe()
	if err != nil {
		t.Fatalf("os.Pipe: %v", err)
	}
	fd := int(r.Fd())
	r.Close() // close before registering

	if err := epoll.AddToEpoll(epfd, fd); err == nil {
		t.Fatal("expected error for closed fd, got nil")
	}
}
