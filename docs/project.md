<!-- markdownlint-disable MD013 -->

# Building a Minimal Epoll Server in C

## Tech Stack

1. Language: C
2. Operating System: Linux

## Libraries

1. `<sys/epoll.h>`

## Abstract

An epoll server is a high-performance network server built on Linux that uses the `epoll` system call for efficient, asynchronous I/O multiplexing.

It allows a single thread to monitor thousands of client connections (file descriptors) simultaneously, notifying the server only when a connection is ready for reading or writing. This makes it significantly more scalable and efficient compared to older mechanisms such as `select` or `poll`.

## System Design

The server consists of the following components:

- Listening socket
- epoll instance
- Event loop
- Connection handler
- I/O handlers

## Components of the Epoll Server

### 1. Listening Socket

This is the server’s main socket responsible for accepting new connections.

1. Created using `socket()`
2. Bound to an address using `bind()`
3. Set to listen using `listen()`
4. Accepts incoming client connections

---

### 2. Epoll Instance

This is the core event manager in the epoll system.

1. Created using `epoll_create()` or `epoll_create1()`
2. Tracks all file descriptors (FDs)
3. Notifies the server when events occur

---

### 3. Registered File Descriptors

These are file descriptors added to the epoll instance using `epoll_ctl()`.

They include:

1. Listening socket FD → detects new connections
2. Client socket FDs → detect incoming data, write readiness, or disconnection

Each FD can be registered with event flags such as:

1. `EPOLLIN` → ready for reading
2. `EPOLLOUT` → ready for writing
3. `EPOLLET` → edge-triggered mode (optional, for performance optimization)

---

### 4. Event Loop

This is the core runtime loop of the server.

1. Waits for events using `epoll_wait()`
2. Receives active file descriptors
3. Processes each event accordingly

Typical flow:

1. Wait for events
2. Iterate over triggered events
3. Handle each event

---

### 5. Connection Handler (Accept Logic)

Triggered when the listening socket is ready (`EPOLLIN`).

1. Accept new connections using `accept()`
2. Set the new socket to non-blocking mode using `fcntl()`
3. Register the new socket with epoll using `epoll_ctl()`

---

### 6. I/O Handlers

These manage client communication.

#### Read Handler

1. Triggered when a socket has `EPOLLIN`
2. Reads data using `read()` or `recv()`

#### Write Handler

1. Triggered when a socket has `EPOLLOUT`
2. Sends data using `write()` or `send()`

---

### 7. Non-blocking Sockets

1. All sockets are set to non-blocking mode using `fcntl()`
2. Prevents the server from blocking on slow or inactive clients

---

### 8. Connection State Management

Tracks per-client state information:

1. Input/output buffers
2. Partial read/write state
3. Protocol state (e.g., HTTP parsing)

Typically implemented using structs mapped to file descriptors.

---

### 9. Event Modes (Optional but Important)

#### Level-Triggered (LT)

1. Default mode
2. Repeatedly notifies if data is still available

#### Edge-Triggered (ET)

1. Notifies only on state changes
2. More efficient but requires careful handling (must read/write until `EAGAIN`)

---

### 10. Cleanup / Connection Teardown

When a client disconnects or an error occurs:

1. Remove FD using `epoll_ctl(..., EPOLL_CTL_DEL, ...)`
2. Close the socket using `close()`
