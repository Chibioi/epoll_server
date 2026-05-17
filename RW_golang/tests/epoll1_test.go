package tests

import (
	"epoll_project/epoll"
	"golang.org/x/sys/unix"
	"net"
	"testing"
)

type mock_listener struct{}

func (m *mock_listener) Accept() (net.Conn, error) { return nil, nil }
func (m *mock_listener) Close() error              { return nil }
func (m *mock_listener) Addr() net.Addr            { return nil }

// Test 1 => tests what happens when you pass a value that is NOT a valid TCP listener
func Test_GRF_Non_TCP_Listener(t *testing.T) {
	_, err := epoll.Get_raw_fd(&mock_listener{})
	if err == nil {
		t.Fatal("expected error for non-TCP listener, got nil")
	}
	if err.Error() != "listener is not a TCP listener" {
		t.Errorf("unexpected error message: %v", err)
	}
}

// Test 2 => test what happens when the value is a valid TCP listener and is Non-blocking
func Test_GRF_Valid_TCP_Listener(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to create TCP listener: %v", err)
	}
	defer ln.Close()
	fd, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if fd <= 0 {
		t.Errorf("expected a valid fd (> 0), got %d", fd)
	}
	// Verify fd is non-blocking
	flags, err := unix.FcntlInt(uintptr(fd), unix.F_GETFL, 0)
	if err != nil {
		t.Fatalf("failed to get fd flags: %v", err)
	}
	if flags&unix.O_NONBLOCK == 0 {
		t.Error("expected fd to be non-blocking, but it is blocking")
	}

	// Clean up the duplicated fd
	unix.Close(fd)
}

// Test 3 => test to ensure that the duplicate is still alive even if the original has been closed
func Test_GRF_FDIsIndependentOfListener(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to create TCP listener: %v", err)
	}

	fd, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	// Close the original listener — the duplicated fd should still be valid
	ln.Close()

	flags, err := unix.FcntlInt(uintptr(fd), unix.F_GETFL, 0)
	if err != nil {
		t.Errorf("fd became invalid after listener was closed: %v", err)
	} else if flags&unix.O_NONBLOCK == 0 {
		t.Error("fd lost non-blocking flag")
	}

	unix.Close(fd)
}

// Test 4 => Ensures multiple calls have distinct file descriptors
func Test_GRF_MultipleCalls_ReturnDistinctFDs(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("failed to create TCP listener: %v", err)
	}
	defer ln.Close()

	fd1, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("first call failed: %v", err)
	}
	fd2, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("second call failed: %v", err)
	}

	if fd1 == fd2 {
		t.Errorf("expected distinct fds on repeated calls, both returned %d", fd1)
	}

	unix.Close(fd1)
	unix.Close(fd2)
}
