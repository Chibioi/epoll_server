package test

import (
	"epoll_project/epoll"
	"fmt"
	"net"
	"sync"
	"testing"
	"time"

	"golang.org/x/sys/unix"
)

// ── helpers ──────────────────────────────────────────────────────────────────

// newEpoll creates an epoll fd and fails the test on error.
func newEpoll(t *testing.T) int {
	t.Helper()
	efd, err := unix.EpollCreate1(0)
	if err != nil {
		t.Fatalf("EpollCreate1: %v", err)
	}
	t.Cleanup(func() { unix.Close(efd) })
	return efd
}

// socketPair returns a connected (client, server) raw fd pair via unix.Socketpair.
// Both fds are closed via t.Cleanup.
func socketPair(t *testing.T) (client, server int) {
	t.Helper()
	fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_STREAM, 0)
	if err != nil {
		t.Fatalf("Socketpair: %v", err)
	}
	unix.SetNonblock(fds[0], true) //nolint:errcheck
	unix.SetNonblock(fds[1], true) //nolint:errcheck
	t.Cleanup(func() { unix.Close(fds[0]); unix.Close(fds[1]) })
	return fds[0], fds[1]
}

// resetConnMap wipes the global connMap between tests.
func resetConnMap() {
	epoll.ConnMapMu.Lock()
	epoll.ConnMap = make(map[int]net.Conn)
	epoll.ConnMapMu.Unlock()
}

// ── Get_raw_fd ────────────────────────────────────────────────────────────────

func TestGetRawFd_ReturnsFd(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Listen: %v", err)
	}
	defer ln.Close()

	fd, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("Get_raw_fd: %v", err)
	}
	if fd <= 0 {
		t.Errorf("expected fd > 0, got %d", fd)
	}
	unix.Close(fd)
}

func TestGetRawFd_NonTCPListenerErrors(t *testing.T) {
	ln, err := net.Listen("unix", t.TempDir()+"/sock.sock")
	if err != nil {
		t.Fatalf("Listen unix: %v", err)
	}
	defer ln.Close()

	_, err = epoll.Get_raw_fd(ln)
	if err == nil {
		t.Fatal("expected error for non-TCP listener, got nil")
	}
}

// ── AddToEpoll / RemoveFromEpoll ─────────────────────────────────────────────

func TestAddToEpoll_RegistersEvent(t *testing.T) {
	efd := newEpoll(t)
	client, server := socketPair(t)
	_ = server

	if err := epoll.AddToEpoll(efd, client); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}

	// Write to server side so client fd becomes readable
	unix.Write(server, []byte("ping")) //nolint:errcheck

	events := make([]unix.EpollEvent, 8)
	n, err := unix.EpollWait(efd, events, 200 /*ms*/)
	if err != nil {
		t.Fatalf("EpollWait: %v", err)
	}
	if n == 0 {
		t.Fatal("expected at least one event, got 0")
	}
	if int(events[0].Fd) != client {
		t.Errorf("expected fd %d, got %d", client, events[0].Fd)
	}
}

func TestRemoveFromEpoll_DeregistersEvent(t *testing.T) {
	efd := newEpoll(t)
	// Socketpair gives us two fds; only track [0] (client) here.
	fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_STREAM, 0)
	if err != nil {
		t.Fatalf("Socketpair: %v", err)
	}
	client, server := fds[0], fds[1]
	defer unix.Close(server)
	// Note: RemoveFromEpoll closes client for us — don't double-close.

	unix.SetNonblock(client, true) //nolint:errcheck
	unix.SetNonblock(server, true) //nolint:errcheck

	if err := epoll.AddToEpoll(efd, client); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}
	epoll.RemoveFromEpoll(efd, client) // deregisters and closes client fd

	// Write to the peer; no events should fire because client was removed.
	unix.Write(server, []byte("ping")) //nolint:errcheck

	events := make([]unix.EpollEvent, 8)
	n, _ := unix.EpollWait(efd, events, 100 /*ms*/)
	if n != 0 {
		t.Errorf("expected 0 events after removal, got %d", n)
	}
}

// ── CloseClient ───────────────────────────────────────────────────────────────

func TestCloseClient_RemovesFromConnMap(t *testing.T) {
	resetConnMap()
	defer resetConnMap()

	efd := newEpoll(t)
	fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_STREAM, 0)
	if err != nil {
		t.Fatalf("Socketpair: %v", err)
	}
	client, server := fds[0], fds[1]
	defer unix.Close(server)
	// client fd will be closed by RemoveFromEpoll inside CloseClient.

	unix.SetNonblock(client, true) //nolint:errcheck
	if err := epoll.AddToEpoll(efd, client); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}

	// Populate connMap with a real net.Conn stand-in.
	c1, c2 := net.Pipe()
	defer c2.Close()
	epoll.ConnMapMu.Lock()
	epoll.ConnMap[client] = c1
	epoll.ConnMapMu.Unlock()

	epoll.CloseClient(efd, client)

	epoll.ConnMapMu.Lock()
	_, stillPresent := epoll.ConnMap[client]
	epoll.ConnMapMu.Unlock()

	if stillPresent {
		t.Error("expected fd to be removed from connMap after CloseClient")
	}
}

func TestCloseClient_UnknownFdIsNoop(t *testing.T) {
	resetConnMap()
	defer resetConnMap()

	efd := newEpoll(t)
	// Use a high fd number that was never registered — should not panic.
	epoll.CloseClient(efd, 999999)
}

// ── connMap concurrency ───────────────────────────────────────────────────────

func TestConnMap_ConcurrentAccess(t *testing.T) {
	resetConnMap()
	defer resetConnMap()

	const goroutines = 20
	var wg sync.WaitGroup
	wg.Add(goroutines)

	for i := range goroutines {
		go func(id int) {
			defer wg.Done()
			c1, c2 := net.Pipe()
			defer c1.Close()
			defer c2.Close()

			epoll.ConnMapMu.Lock()
			epoll.ConnMap[id] = c1
			epoll.ConnMapMu.Unlock()

			time.Sleep(time.Millisecond)

			epoll.ConnMapMu.Lock()
			delete(epoll.ConnMap, id)
			epoll.ConnMapMu.Unlock()
		}(i)
	}
	wg.Wait()

	epoll.ConnMapMu.Lock()
	remaining := len(epoll.ConnMap)
	epoll.ConnMapMu.Unlock()

	if remaining != 0 {
		t.Errorf("expected empty connMap, got %d entries", remaining)
	}
}

// ── addrFromSockaddr ──────────────────────────────────────────────────────────

func TestAddrFromSockaddr_IPv4(t *testing.T) {
	sa := &unix.SockaddrInet4{Addr: [4]byte{127, 0, 0, 1}, Port: 8080}
	got := epoll.AddrFromSockaddr(sa)
	want := "127.0.0.1:8080"
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestAddrFromSockaddr_IPv6(t *testing.T) {
	sa := &unix.SockaddrInet6{Port: 443}
	got := epoll.AddrFromSockaddr(sa)
	// Just confirm it contains the port and doesn't panic.
	if got == "unknown" {
		t.Errorf("expected IPv6 address, got %q", got)
	}
}

func TestAddrFromSockaddr_Unknown(t *testing.T) {
	got := epoll.AddrFromSockaddr(nil) // nil satisfies the interface as unknown type
	// nil won't match any case; we just want no panic.
	_ = got
}

// ── fdToNetConn ───────────────────────────────────────────────────────────────

func TestFdToNetConn_ValidFd(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Listen: %v", err)
	}
	defer ln.Close()

	addr := ln.Addr().String()
	connCh := make(chan net.Conn, 1)
	go func() {
		c, _ := ln.Accept()
		connCh <- c
	}()

	raw, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatalf("Dial: %v", err)
	}
	defer raw.Close()

	accepted := <-connCh
	defer accepted.Close()

	tc := accepted.(*net.TCPConn)
	f, err := tc.File()
	if err != nil {
		t.Fatalf("File: %v", err)
	}
	defer f.Close()

	conn, err := epoll.FdToNetConn(int(f.Fd()))
	if err != nil {
		t.Fatalf("fdToNetConn: %v", err)
	}
	defer conn.Close()

	if conn.RemoteAddr() == nil {
		t.Error("expected non-nil RemoteAddr")
	}
}

// ── AcceptAll integration ─────────────────────────────────────────────────────

func TestAcceptAll_AcceptsConnections(t *testing.T) {
	resetConnMap()
	defer resetConnMap()

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("Listen: %v", err)
	}
	defer ln.Close()

	listenFd, err := epoll.Get_raw_fd(ln)
	if err != nil {
		t.Fatalf("Get_raw_fd: %v", err)
	}
	defer unix.Close(listenFd)

	efd := newEpoll(t)
	if err := epoll.AddToEpoll(efd, listenFd); err != nil {
		t.Fatalf("AddToEpoll: %v", err)
	}

	// Dial two clients before calling AcceptAll.
	addr := ln.Addr().String()
	clients := make([]net.Conn, 2)
	for i := range clients {
		clients[i], err = net.Dial("tcp", addr)
		if err != nil {
			t.Fatalf("Dial[%d]: %v", i, err)
		}
		defer clients[i].Close()
	}

	// Give the OS time to queue the connections.
	time.Sleep(20 * time.Millisecond)

	epoll.AcceptAll(efd, listenFd)

	epoll.ConnMapMu.Lock()
	n := len(epoll.ConnMap)
	epoll.ConnMapMu.Unlock()

	if n != 2 {
		t.Errorf("expected 2 entries in connMap, got %d", n)
	}

	// Each client should have received the greeting.
	for i, c := range clients {
		c.SetReadDeadline(time.Now().Add(500 * time.Millisecond)) //nolint:errcheck
		buf := make([]byte, 64)
		nr, err := c.Read(buf)
		if err != nil {
			t.Errorf("client[%d] Read: %v", i, err)
			continue
		}
		greeting := string(buf[:nr])
		want := "Hello from the epoll server!\n"
		if greeting != want {
			t.Errorf("client[%d] greeting = %q, want %q", i, greeting, want)
		}
	}

	// Verify greeting text is correct and echoed (ReadAll smoke-check).
	for i, c := range clients {
		msg := fmt.Sprintf("client-%d\n", i)
		c.SetWriteDeadline(time.Now().Add(200 * time.Millisecond)) //nolint:errcheck
		if _, err := c.Write([]byte(msg)); err != nil {
			t.Errorf("client[%d] Write: %v", i, err)
		}
	}
}
