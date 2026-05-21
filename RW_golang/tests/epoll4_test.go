package tests

import (
	"epoll_project/epoll"
	"golang.org/x/sys/unix"
	"net"
	"os"
	"strings"
	"testing"
	"time"
)

func TestFdToNetConn(t *testing.T) {
	t.Run("valid TCP socket fd returns usable net.Conn", func(t *testing.T) {
		// Use a real TCP socketpair via a listener so both ends are genuine TCP
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			t.Fatalf("failed to create listener: %v", err)
		}
		defer ln.Close()

		clientConn, err := net.Dial("tcp", ln.Addr().String()) // connect to the address of the listener
		if err != nil {
			t.Fatalf("failed to dial: %v", err)
		}
		defer clientConn.Close()

		serverConn, err := ln.Accept()
		if err != nil {
			t.Fatalf("failed to accept: %v", err)
		}
		defer serverConn.Close()

		// Extract the raw fd from the server side
		tcpConn := serverConn.(*net.TCPConn)
		rawConn, err := tcpConn.SyscallConn()
		if err != nil {
			t.Fatalf("failed to get SyscallConn: %v", err)
		}

		var serverFd int
		err = rawConn.Control(func(fd uintptr) {
			// dup so FdToNetConn can close its wrapper independently
			// without affecting the original serverConn
			serverFd, err = unix.Dup(int(fd))
			if err != nil {
				t.Errorf("failed to dup fd: %v", err)
			}
		})
		if err != nil || serverFd == 0 {
			t.Fatalf("control/dup failed: %v", err)
		}

		// Call function under test
		conn, err := epoll.FdToNetConn(serverFd)
		if err != nil {
			t.Fatalf("FdToNetConn returned unexpected error: %v", err)
		}
		defer conn.Close()

		// Verify the returned conn is functional: send from client, receive via conn
		want := "hello from client"
		_, err = clientConn.Write([]byte(want))
		if err != nil {
			t.Fatalf("client write failed: %v", err)
		}

		buf := make([]byte, len(want))
		conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		n, err := conn.Read(buf)
		if err != nil {
			t.Fatalf("read on FdToNetConn result failed: %v", err)
		}
		if got := string(buf[:n]); got != want {
			t.Errorf("got %q, want %q", got, want)
		}

		// Verify the wrapper fd was closed by os.NewFile.Close() (internal to FdToNetConn)
		// The duped fd should be closed; attempting to dup it again should return EBADF
		_, dupErr := unix.Dup(serverFd)
		if dupErr != unix.EBADF {
			t.Errorf("expected serverFd to be closed after FdToNetConn, got: %v", dupErr)
		}
	})

	t.Run("invalid fd returns error", func(t *testing.T) {
		// fd -1 is always invalid; os.NewFile returns nil
		conn, err := epoll.FdToNetConn(-1)
		if err == nil {
			conn.Close()
			t.Fatal("expected error for fd=-1, got nil")
		}
		if conn != nil {
			t.Errorf("expected nil conn on error, got %v", conn)
		}
		if !strings.Contains(err.Error(), "os.NewFile returned nil") {
			t.Errorf("unexpected error message: %v", err)
		}
	})

	t.Run("non-socket fd returns net.FileConn error", func(t *testing.T) {
		// A regular file is a valid fd but not a socket; net.FileConn must reject it
		f, err := os.CreateTemp(t.TempDir(), "fdtest-*")
		if err != nil {
			t.Fatalf("failed to create temp file: %v", err)
		}
		defer f.Close()

		// Dup so FdToNetConn can close its os.File wrapper without closing f
		dupFd, err := unix.Dup(int(f.Fd()))
		if err != nil {
			t.Fatalf("failed to dup temp file fd: %v", err)
		}

		conn, err := epoll.FdToNetConn(dupFd)
		if err == nil {
			conn.Close()
			t.Fatal("expected error for non-socket fd, got nil")
		}
		if conn != nil {
			t.Errorf("expected nil conn on error, got %v", conn)
		}
		if !strings.Contains(err.Error(), "net.FileConn") {
			t.Errorf("unexpected error message: %v", err)
		}
	})
}
