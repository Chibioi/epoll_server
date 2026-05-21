package tests

import (
	"epoll_project/epoll"
	"golang.org/x/sys/unix"
	"os"
	"testing"
)

func TestRemoveFromEpoll(t *testing.T) {
	// Create an epoll instance
	epollFd, err := unix.EpollCreate1(0)
	if err != nil {
		t.Fatalf("failed to create epoll: %v", err)
	}
	defer unix.Close(epollFd)

	// Create a real file descriptor using a pipe
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("failed to create pipe: %v", err)
	}
	defer w.Close() // only close write end; read end is managed by RemoveFromEpoll

	fd := int(r.Fd())

	// Register the FD with epoll before removing it
	event := &unix.EpollEvent{
		Events: unix.EPOLLIN,
		Fd:     int32(fd),
	}
	if err := unix.EpollCtl(epollFd, unix.EPOLL_CTL_ADD, fd, event); err != nil {
		t.Fatalf("failed to add fd to epoll: %v", err)
	}

	// Verify the FD is registered (adding it again should fail with EEXIST)
	err = unix.EpollCtl(epollFd, unix.EPOLL_CTL_ADD, fd, event)
	if err != unix.EEXIST {
		t.Fatalf("expected EEXIST before removal, got: %v", err)
	}

	// Call the function under test
	epoll.RemoveFromEpoll(epollFd, fd)

	// Verify FD is deregistered: adding it again should now fail with EBADF (fd is closed)
	// or succeed if the fd number was reused — but since we just closed it, EBADF is expected
	err = unix.EpollCtl(epollFd, unix.EPOLL_CTL_ADD, fd, event)
	if err != unix.EBADF {
		t.Errorf("expected EBADF after RemoveFromEpoll (fd should be closed), got: %v", err)
	}

	// Double-check: attempt to use the fd directly — should fail
	var buf [1]byte
	_, readErr := unix.Read(fd, buf[:])
	if readErr != unix.EBADF {
		t.Errorf("expected EBADF on read after close, got: %v", readErr)
	}
}
