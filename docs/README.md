<!-- markdownlint-disable MD013 -->

# Building a Minimal Epoll Server in Go

## Tech Stack

1. Language: Go
2. Operating System: Linux
3. Testing framework: Go's built-in `testing` package

## Libraries

1. `golang.org/x/sys/unix` — low-level epoll syscall wrappers
2. `net` — higher-level TCP connection abstractions
3. `sync` — mutex for concurrent connection map access

## Abstract

An epoll server is a high-performance network server built on Linux that uses the `epoll` system call for efficient, asynchronous I/O multiplexing.

It allows a single goroutine to monitor thousands of client connections (file descriptors) simultaneously, notifying the server only when a connection is ready for reading or writing. This makes it significantly more scalable and efficient compared to older mechanisms such as `select` or `poll`.

## Project Structure

```
.
├── README.md
└── RW_golang
    ├── epoll
    │   └── epoll.go        # Core epoll server logic
    ├── go.mod
    ├── go.sum
    ├── main.go             # Entrypoint
    └── tests
        ├── epoll1_test.go  # Get_raw_fd tests
        ├── epoll2_test.go  # AddToEpoll tests
        ├── epoll3_test.go  # RemoveFromEpoll tests
        └── epoll4_test.go  # FdToNetConn tests
```

## System Design

The server consists of the following components:

- Listening socket
- Epoll instance
- Event loop
- Connection handler
- I/O handlers
- Write buffer with `EPOLLOUT` re-arming
- Connection state map

## Components of the Epoll Server

### 1. Listening Socket

This is the server's main socket responsible for accepting new connections.

1. Created using `net.Listen("tcp", ":2551")`
2. The raw file descriptor is extracted using `Get_raw_fd()`
3. Set to non-blocking mode via `unix.SetNonblock()`
4. Accepts incoming client connections at the syscall level with `unix.Accept()`

---

### 2. Epoll Instance

This is the core event manager in the epoll system.

1. Created using `unix.EpollCreate1(0)`
2. Tracks all file descriptors (FDs) registered via `unix.EpollCtl()`
3. Notifies the server when events occur via `unix.EpollWait()`

---

### 3. Registered File Descriptors

These are file descriptors added to the epoll instance using `unix.EpollCtl()` with `unix.EPOLL_CTL_ADD`.

They include:

1. Listening socket FD → detects new connections (`EPOLLIN`)
2. Client socket FDs → detect incoming data or disconnection (`EPOLLIN | EPOLLET`)

Each FD is registered with event flags:

1. `unix.EPOLLIN` → ready for reading
2. `unix.EPOLLET` → edge-triggered mode for performance

```go
func AddToEpoll(epoll_fd, fd int) error {
    ev := unix.EpollEvent{
        Events: unix.EPOLLIN | unix.EPOLLET,
        Fd:     int32(fd),
    }
    return unix.EpollCtl(epoll_fd, unix.EPOLL_CTL_ADD, fd, &ev)
}
```

---

### 4. Event Loop

This is the core runtime loop of the server, running in `RunEpollServer()`.

1. Waits for events using `unix.EpollWait(epoll_fd, events, -1)`
2. Iterates over **only the triggered events** using `events[:n]` — iterating the full slice would process stale events from the previous iteration
3. Dispatches each event to the appropriate handler

Typical flow:

1. Block on `EpollWait`
2. Iterate over `events[:n]`
3. If `fd == listen_fd` → call `AcceptAll()`
4. If `EPOLLOUT` is set → call `FlushWriteBuf()` to drain the per-conn write buffer
5. If `EPOLLIN` is set → call `ReadAll()`

---

### 5. Connection Handler — `AcceptAll()`

Triggered when the listening socket receives `EPOLLIN`.

1. Drains all pending connections in a loop using `unix.Accept()`
2. Stops when `unix.EAGAIN` or `unix.EWOULDBLOCK` is returned (EPOLLET requirement)
3. Sets each new client FD to non-blocking via `unix.SetNonblock()`
4. Wraps the raw FD into a `net.Conn` using `FdToNetConn()`, which internally dups the FD
5. Extracts the dup'd inner FD from the `net.Conn` via `SyscallConn().Control()` — this is the FD epoll must watch, not the original `conn_fd`
6. Closes the original `conn_fd` since `net.Conn` now owns its own dup'd copy
7. Stores the connection as a `*Conn` struct in `ConnMap`, keyed by `innerFd`
8. Registers `innerFd` with epoll via `AddToEpoll()`

```go
func AcceptAll(epoll_fd, listen_fd int) {
    for {
        conn_fd, sa, err := unix.Accept(listen_fd)
        if errors.Is(err, unix.EAGAIN) || errors.Is(err, unix.EWOULDBLOCK) {
            return // drained
        }
        // wrap, extract innerFd, close conn_fd, store &Conn{}, register innerFd
    }
}
```

---

### 6. I/O Handlers

#### Read Handler — `ReadAll()`

1. Triggered when a client FD has `EPOLLIN`
2. Reads data in a loop using `unix.Read()` into a 4096-byte buffer
3. Stops on `EAGAIN`/`EWOULDBLOCK` (edge-triggered drain requirement)
4. On `n == 0` (EOF) or a real error, calls `CloseClient()` to clean up
5. Accumulates received bytes into `c.readBuf` on the `*Conn` struct — required for HTTP parsing where a complete request may span multiple `EPOLLIN` events
6. Echoes received bytes back to the client via `WriteAll()`

#### Write Handler — `WriteAll()` and `FlushWriteBuf()`

`WriteAll()` writes as much data as possible in a loop. When the kernel send buffer is full and `unix.Write()` returns `EAGAIN`:

1. The unwritten remainder is appended to `c.writeBuf` on the `*Conn` struct
2. The FD is re-armed with `EPOLLIN | EPOLLOUT | EPOLLET` so epoll notifies when it becomes writable again
3. `FlushWriteBuf()` is called on the next `EPOLLOUT` event to drain `c.writeBuf`
4. Once the buffer is empty the FD is re-armed back to `EPOLLIN | EPOLLET` only

This ensures no data is silently dropped when the kernel send buffer is full.

```go
func WriteAll(fd int, data []byte) error {
    for len(data) > 0 {
        n, err := unix.Write(fd, data)
        if errors.Is(err, unix.EAGAIN) {
            // store remainder, re-arm EPOLLOUT
            return nil
        }
        if errors.Is(err, unix.EINTR) { continue }
        if err != nil { return err }
        data = data[n:]
    }
    return nil
}
```

---

### 7. Non-blocking Sockets

All sockets are set to non-blocking mode using `unix.SetNonblock(fd, true)`.

This prevents the server from blocking on slow or inactive clients, which is essential for correct edge-triggered epoll behaviour (reads/writes must loop until `EAGAIN`).

---

### 8. Connection State Management — `ConnMap`

Tracks per-client connections as a `map[int]*Conn`, keyed by the inner FD owned by `net.Conn`.

```go
type Conn struct {
    fd       int
    netConn  net.Conn
    readBuf  []byte // accumulated incoming bytes
    writeBuf []byte // bytes waiting to be flushed
}

var (
    ConnMap   = make(map[int]*Conn)
    ConnMapMu sync.Mutex
)
```

Each entry is a `*Conn` struct rather than a bare `net.Conn`, giving every connection its own read and write buffers. `readBuf` accumulates partial reads across multiple `EPOLLIN` events (needed for HTTP parsing). `writeBuf` holds data that could not be written immediately due to a full kernel send buffer. Access is protected by `ConnMapMu sync.Mutex` since the map is shared across handler calls.

---

### 9. `Get_raw_fd()` — Extracting the Raw File Descriptor

Go's `net.Listener` does not expose its FD directly. `Get_raw_fd()` handles this safely:

1. Type-asserts `net.Listener` to `*net.TCPListener`
2. Calls `tcpListen.File()` to get a duplicate FD wrapped in an `*os.File`
3. Calls `unix.Dup()` to create a second duplicate that stays alive after `file.Close()`
4. Sets the duplicated FD to non-blocking mode

```go
func Get_raw_fd(Listen net.Listener) (int, error) {
    tcpListen, ok := Listen.(*net.TCPListener)
    file, err := tcpListen.File()
    defer file.Close()
    fd, err := unix.Dup(int(file.Fd()))
    unix.SetNonblock(fd, true)
    return fd, nil
}
```

---

### 10. `FdToNetConn()` — Wrapping a Raw FD as `net.Conn`

Client FDs are accepted at the syscall level. This helper wraps them back into `net.Conn` so higher-level Go I/O can be used where needed:

```go
func FdToNetConn(fd int) (net.Conn, error) {
    f := os.NewFile(uintptr(fd), fmt.Sprintf("tcp-conn-%d", fd))
    conn, err := net.FileConn(f)
    f.Close() // FileConn dups fd internally; close the wrapper
    return conn, err
}
```

---

### 11. Event Modes

#### Edge-Triggered (ET) — `unix.EPOLLET`

This server uses edge-triggered mode exclusively. Notifications fire only on state changes (e.g. new data arriving), so every handler **must** read or accept in a loop until `EAGAIN` to avoid missing events.

#### Level-Triggered (LT)

The default epoll mode — repeatedly notifies as long as data is available. Not used here.

---

### 12. Cleanup — `CloseClient()` and `RemoveFromEpoll()`

When a client disconnects or an error occurs:

1. `RemoveFromEpoll()` calls `unix.EpollCtl(..., EPOLL_CTL_DEL, ...)` to deregister the FD — it no longer closes the FD directly, since `net.Conn` owns the underlying descriptor
2. `CloseClient()` deregisters from epoll **first**, then deletes from `ConnMap`, then closes via `c.netConn.Close()` — this order prevents a race where epoll fires one last event on an fd that has already been closed

```go
func CloseClient(epoll_fd, fd int) {
    ConnMapMu.Lock()
    c, ok := ConnMap[fd]
    if !ok { ConnMapMu.Unlock(); return }
    delete(ConnMap, fd)
    ConnMapMu.Unlock()

    RemoveFromEpoll(epoll_fd, fd) // deregister before closing
    c.netConn.Close()             // closes the dup'd fd owned by net.Conn
}
```

`unix.Close(fd)` is intentionally absent — calling it alongside `c.netConn.Close()` would be a double-close, which can silently close an unrelated FD that the OS reused for a new connection.

---

## How the Server Works

1. Call `net.Listen("tcp", ":2551")` to create the listening socket
2. Extract the raw FD with `Get_raw_fd()`
3. Create an epoll instance with `unix.EpollCreate1(0)`
4. Register the listening FD with `AddToEpoll()`
5. Enter the event loop — block on `unix.EpollWait()`
6. On `listen_fd` event → `AcceptAll()` (accepts, registers, greets clients)
7. On `EPOLLOUT` client event → `FlushWriteBuf()` (drains the pending write buffer)
8. On `EPOLLIN` client event → `ReadAll()` (reads, accumulates into `readBuf`, echoes data)
9. On disconnect or error → `CloseClient()` (deregisters then closes)

---

## Implementation Details

The server uses:

- Non-blocking sockets via `unix.SetNonblock()`
- Event-driven I/O using the `unix.EpollCreate1` / `unix.EpollCtl` / `unix.EpollWait` triad
- Edge-triggered (`EPOLLET`) event mode with drain loops
- A mutex-protected `map[int]*Conn` for per-connection state, read buffers, and write buffers
- `unix.Dup()` to safely extract raw FDs from Go's `net.Listener`
- `net.FileConn()` to bridge raw FDs back to `net.Conn`, with `innerFd` extracted via `SyscallConn().Control()` so epoll watches the correct descriptor
- `EPOLLOUT` re-arming when the kernel send buffer is full, with `FlushWriteBuf()` draining the backlog on the next writable event

This design avoids blocking calls and allows a single goroutine to handle many clients efficiently.

---

## Running the Server

```bash
cd RW_golang
go run main/main.go
```

The server listens on `:2551` and logs new connections and received data to stdout.

---

## Running the Tests

```bash
cd RW_golang
go test ./tests/...
```

---

## Key Takeaways

- `epoll` is more scalable than `select` or `poll`
- Non-blocking I/O is essential for correct edge-triggered behaviour
- Edge-triggered mode requires draining reads/accepts until `EAGAIN`
- `unix.Dup()` is necessary to safely extract FDs from Go's `net` abstractions
- `net.FileConn()` internally dups the FD — always use `innerFd` from `SyscallConn().Control()` as the epoll key, not the original `conn_fd`
- Never call both `unix.Close(fd)` and `net.Conn.Close()` on the same connection — pick one owner
- Always deregister from epoll before closing the FD to prevent a race on the last event
- A mutex-protected `map[int]*Conn` with per-conn read/write buffers is the foundation for any application protocol on top of raw epoll

---

## Conclusion

This minimal epoll server demonstrates how Linux efficiently handles high-performance network I/O, implemented idiomatically in Go using the `golang.org/x/sys/unix` package.

Future improvements could include:

- HTTP protocol parsing (next milestone)
- Goroutine pool integration for CPU-bound request handling
- Connection timeouts and idle connection pruning
- Graceful shutdown handling
