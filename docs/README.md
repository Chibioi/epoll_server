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
├── docs
│   └── README.md
└── RW_golang
    ├── epoll
    │   └── epoll.go        # Core epoll server logic
    ├── go.mod
    ├── go.sum
    ├── main
    │   ├── main
    │   └── main.go         # Entrypoint
    └── tests
        └── epoll1_test.go  # Unit tests
```

## System Design

The server consists of the following components:

- Listening socket
- Epoll instance
- Event loop
- Connection handler
- I/O handlers
- Connection state map

## Components of the Epoll Server

### 1. Listening Socket

This is the server's main socket responsible for accepting new connections.

1. Created using `net.Listen("tcp", ":8080")`
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
2. Iterates over triggered `unix.EpollEvent` entries
3. Dispatches each event to the appropriate handler

Typical flow:

1. Block on `EpollWait`
2. Iterate over triggered events
3. If `fd == listen_fd` → call `AcceptAll()`
4. Otherwise → call `ReadAll()`

---

### 5. Connection Handler — `AcceptAll()`

Triggered when the listening socket receives `EPOLLIN`.

1. Drains all pending connections in a loop using `unix.Accept()`
2. Stops when `unix.EAGAIN` or `unix.EWOULDBLOCK` is returned (EPOLLET requirement)
3. Sets each new client FD to non-blocking via `unix.SetNonblock()`
4. Wraps the raw FD into a `net.Conn` using `FdToNetConn()` for higher-level I/O
5. Registers the new FD with epoll via `AddToEpoll()`
6. Stores the connection in the shared `ConnMap`

```go
func AcceptAll(epoll_fd, listen_fd int) {
    for {
        conn_fd, sa, err := unix.Accept(listen_fd)
        if errors.Is(err, unix.EAGAIN) || errors.Is(err, unix.EWOULDBLOCK) {
            return // drained
        }
        // ... set non-blocking, wrap to net.Conn, register with epoll
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
5. Echoes received bytes back to the client with `unix.Write()`

#### Write

Currently handled inline — the server sends a greeting immediately after accepting a new connection:

```go
unix.Write(conn_fd, []byte("Hello from the epoll server!\n"))
```

---

### 7. Non-blocking Sockets

All sockets are set to non-blocking mode using `unix.SetNonblock(fd, true)`.

This prevents the server from blocking on slow or inactive clients, which is essential for correct edge-triggered epoll behaviour (reads/writes must loop until `EAGAIN`).

---

### 8. Connection State Management — `ConnMap`

Tracks per-client connections as a `map[int]net.Conn`, keyed by raw file descriptor.

```go
var (
    ConnMap   = make(map[int]net.Conn)
    ConnMapMu sync.Mutex
)
```

Access is protected by `ConnMapMu sync.Mutex` since the map is shared across handler calls. The `net.Conn` value enables use of Go's higher-level helpers (e.g. `RemoteAddr`, `bufio`) alongside the raw FD used for epoll.

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

1. `RemoveFromEpoll()` calls `unix.EpollCtl(..., EPOLL_CTL_DEL, ...)` then `unix.Close(fd)`
2. `CloseClient()` also closes the associated `net.Conn` and removes the entry from `ConnMap`

```go
func CloseClient(epoll_fd, fd int) {
    RemoveFromEpoll(epoll_fd, fd)
    ConnMapMu.Lock()
    if conn, ok := ConnMap[fd]; ok {
        conn.Close()
        delete(ConnMap, fd)
    }
    ConnMapMu.Unlock()
}
```

---

## How the Server Works

1. Call `net.Listen("tcp", ":8080")` to create the listening socket
2. Extract the raw FD with `Get_raw_fd()`
3. Create an epoll instance with `unix.EpollCreate1(0)`
4. Register the listening FD with `AddToEpoll()`
5. Enter the event loop — block on `unix.EpollWait()`
6. On `listen_fd` event → `AcceptAll()` (accepts, registers, greets clients)
7. On client FD event → `ReadAll()` (reads and echoes data)
8. On disconnect or error → `CloseClient()` (deregisters and frees resources)

---

## Implementation Details

The server uses:

- Non-blocking sockets via `unix.SetNonblock()`
- Event-driven I/O using the `unix.EpollCreate1` / `unix.EpollCtl` / `unix.EpollWait` triad
- Edge-triggered (`EPOLLET`) event mode with drain loops
- A mutex-protected `map[int]net.Conn` for per-connection state
- `unix.Dup()` to safely extract raw FDs from Go's `net.Listener`
- `net.FileConn()` to bridge raw FDs back to `net.Conn`

This design avoids blocking calls and allows a single goroutine to handle many clients efficiently.

---

## Running the Server

```bash
cd RW_golang
go run main/main.go
```

The server listens on `:8080` and logs new connections and received data to stdout.

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
- A mutex-protected map bridges raw FDs and `net.Conn` for clean state management

---

## Conclusion

This minimal epoll server demonstrates how Linux efficiently handles high-performance network I/O, implemented idiomatically in Go using the `golang.org/x/sys/unix` package.

Future improvements could include:

- HTTP protocol parsing
- Goroutine pool integration
- Write buffering with `EPOLLOUT` registration
- Connection pooling
- Graceful shutdown handling
