/*
 * test_epoll.c — CUnit tests for epoll.c
 *
 * Build:
 *   gcc -o test_epoll test_epoll.c epoll.c -lcunit
 *
 * Run:
 *   ./test_epoll
 */

#include "../src/epoll.h"
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Connected non-blocking socket pair: fds[0] = server side, fds[1] = client
 * side */
static int make_pair(int fds[2]) {
  return socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds);
}

/* Register fds[0] with a fresh epoll instance and return the epoll fd */
static int make_epoll_with(int fd, uint32_t events) {
  int efd = create_epoll_instance();
  struct epoll_event ev = {.events = events, .data.fd = fd};
  epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
  return efd;
}

static void reset_buf(int fd) {
  write_buffers[fd].len = 0;
  memset(write_buffers[fd].buf, 0, BUF_SIZE);
}

/* ── suite hooks ─────────────────────────────────────────────────────────── */

static int suite_init(void) { return 0; }
static int suite_clean(void) { return 0; }

/* ══════════════════════════════════════════════════════════════════════════
 * create_socket
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_create_socket_valid_fd(void) {
  int fd = create_socket();
  CU_ASSERT(fd >= 0);
  close(fd);
}

static void test_create_socket_is_tcp(void) {
  int fd = create_socket();
  int type = 0;
  socklen_t len = sizeof(type);
  getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len);
  CU_ASSERT_EQUAL(type, SOCK_STREAM);
  close(fd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * set_socket_opt
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_set_socket_opt_reuseaddr(void) {
  int fd = create_socket();
  set_socket_opt(fd);
  int val = 0;
  socklen_t len = sizeof(val);
  getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, &len);
  CU_ASSERT(val != 0);
  close(fd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * setnonblocking
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_setnonblocking_sets_flag(void) {
  int fd = create_socket();
  CU_ASSERT(setnonblocking(fd) != -1);
  CU_ASSERT(fcntl(fd, F_GETFL, 0) & O_NONBLOCK);
  close(fd);
}

static void test_setnonblocking_idempotent(void) {
  int fd = create_socket();
  setnonblocking(fd);
  CU_ASSERT(setnonblocking(fd) != -1); /* second call must succeed */
  CU_ASSERT(fcntl(fd, F_GETFL, 0) & O_NONBLOCK);
  close(fd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * create_epoll_instance
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_create_epoll_instance_valid_fd(void) {
  int efd = create_epoll_instance();
  CU_ASSERT(efd >= 0);
  close(efd);
}

static void test_create_epoll_instance_usable(void) {
  int efd = create_epoll_instance();
  int pair[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
  struct epoll_event ev = {.events = EPOLLIN, .data.fd = pair[0]};
  CU_ASSERT_EQUAL(epoll_ctl(efd, EPOLL_CTL_ADD, pair[0], &ev), 0);
  close(pair[0]);
  close(pair[1]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_use_fd — bounds guard
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_use_fd_rejects_negative(void) {
  int efd = create_epoll_instance();
  /* Sentinel: write_buffers[0] must be untouched when fd == -1 */
  write_buffers[0].len = 99;
  do_use_fd(-1, efd);
  CU_ASSERT_EQUAL(write_buffers[0].len, 99);
  write_buffers[0].len = 0;
  close(efd);
}

static void test_do_use_fd_rejects_gte_max(void) {
  int efd = create_epoll_instance();
  do_use_fd(MAX_CLIENTS, efd); /* must not crash or corrupt memory */
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_use_fd — EOF clears the write buffer
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_use_fd_eof_clears_buffer(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLET);

  /* pre-load stale data */
  memcpy(write_buffers[pair[0]].buf, "stale", 5);
  write_buffers[pair[0]].len = 5;

  close(pair[1]); /* EOF on the server side */
  do_use_fd(pair[0], efd);

  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 0);
  /* pair[0] was closed inside do_use_fd */
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_use_fd — data is queued into the write buffer
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_use_fd_queues_data(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLET);
  reset_buf(pair[0]);

  write(pair[1], "hello", 5);
  do_use_fd(pair[0], efd);

  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 5);
  CU_ASSERT_EQUAL(memcmp(write_buffers[pair[0]].buf, "hello", 5), 0);

  reset_buf(pair[0]);
  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_use_fd — overflow is clamped to BUF_SIZE
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_use_fd_clamps_overflow(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLET);

  /* Leave only 10 bytes of space */
  reset_buf(pair[0]);
  write_buffers[pair[0]].len = BUF_SIZE - 10;

  char data[20];
  memset(data, 'X', sizeof(data));
  write(pair[1], data, sizeof(data));
  do_use_fd(pair[0], efd);

  /* Buffer must be exactly full, never overflowed */
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, (size_t)BUF_SIZE);

  reset_buf(pair[0]);
  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_use_fd — multiple sequential reads accumulate correctly
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_use_fd_accumulates_multiple_writes(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLET);
  reset_buf(pair[0]);

  write(pair[1], "foo", 3);
  do_use_fd(pair[0], efd);
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 3);

  write(pair[1], "bar", 3);
  do_use_fd(pair[0], efd);
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 6);
  CU_ASSERT_EQUAL(memcmp(write_buffers[pair[0]].buf, "foobar", 6), 0);

  reset_buf(pair[0]);
  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_write_fd — bounds guard
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_write_fd_rejects_negative(void) {
  int efd = create_epoll_instance();
  do_write_fd(-1, efd); /* must not crash */
  close(efd);
}

static void test_do_write_fd_rejects_gte_max(void) {
  int efd = create_epoll_instance();
  do_write_fd(MAX_CLIENTS, efd); /* must not crash */
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_write_fd — flushes write buffer to the peer
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_write_fd_sends_data(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLOUT | EPOLLET);

  memcpy(write_buffers[pair[0]].buf, "world", 5);
  write_buffers[pair[0]].len = 5;

  do_write_fd(pair[0], efd);

  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 0); /* buffer drained */

  char recv[16] = {0};
  CU_ASSERT_EQUAL(read(pair[1], recv, sizeof(recv)), 5);
  CU_ASSERT_EQUAL(memcmp(recv, "world", 5), 0);

  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_write_fd — empty buffer does nothing
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_write_fd_empty_buffer_noop(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLOUT | EPOLLET);
  reset_buf(pair[0]);

  do_write_fd(pair[0], efd);

  /* Nothing should arrive at the peer */
  char recv[16];
  ssize_t n = read(pair[1], recv, sizeof(recv));
  CU_ASSERT(n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));

  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * do_write_fd — disarms EPOLLOUT when buffer fully drained
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_do_write_fd_disarms_epollout_when_drained(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLOUT | EPOLLET);

  memcpy(write_buffers[pair[0]].buf, "hi", 2);
  write_buffers[pair[0]].len = 2;

  do_write_fd(pair[0], efd);
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 0);

  /*
   * After draining, epoll_ctl MOD strips EPOLLOUT.
   * epoll_wait with timeout=0 should no longer return an EPOLLOUT event
   * for this fd (the socket is idle — no write interest registered).
   */
  struct epoll_event out;
  int nfds = epoll_wait(efd, &out, 1, 0);
  if (nfds == 1) {
    CU_ASSERT(!(out.events & EPOLLOUT));
  }

  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Round-trip echo: client → do_use_fd → do_write_fd → client
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_roundtrip_echo(void) {
  int pair[2];
  CU_ASSERT_EQUAL(make_pair(pair), 0);
  int efd = make_epoll_with(pair[0], EPOLLIN | EPOLLOUT | EPOLLET);
  reset_buf(pair[0]);

  /* client sends */
  CU_ASSERT_EQUAL(write(pair[1], "ping", 4), 4);

  /* server reads and queues */
  do_use_fd(pair[0], efd);
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 4);
  CU_ASSERT_EQUAL(memcmp(write_buffers[pair[0]].buf, "ping", 4), 0);

  /* server flushes back */
  do_write_fd(pair[0], efd);
  CU_ASSERT_EQUAL(write_buffers[pair[0]].len, 0);

  /* client receives the echo */
  char recv[16] = {0};
  CU_ASSERT_EQUAL(read(pair[1], recv, sizeof(recv)), 4);
  CU_ASSERT_EQUAL(memcmp(recv, "ping", 4), 0);

  close(pair[1]);
  close(pair[0]);
  close(efd);
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void) {
  if (CU_initialize_registry() != CUE_SUCCESS)
    return CU_get_error();

  CU_pSuite s = CU_add_suite("epoll_server", suite_init, suite_clean);
  if (!s) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  /* create_socket */
  CU_add_test(s, "create_socket: returns valid fd",
              test_create_socket_valid_fd);
  CU_add_test(s, "create_socket: socket is TCP", test_create_socket_is_tcp);

  /* set_socket_opt */
  CU_add_test(s, "set_socket_opt: SO_REUSEADDR enabled",
              test_set_socket_opt_reuseaddr);

  /* setnonblocking */
  CU_add_test(s, "setnonblocking: sets O_NONBLOCK",
              test_setnonblocking_sets_flag);
  CU_add_test(s, "setnonblocking: idempotent", test_setnonblocking_idempotent);

  /* create_epoll_instance */
  CU_add_test(s, "create_epoll_instance: valid fd",
              test_create_epoll_instance_valid_fd);
  CU_add_test(s, "create_epoll_instance: usable",
              test_create_epoll_instance_usable);

  /* do_use_fd */
  CU_add_test(s, "do_use_fd: rejects fd < 0", test_do_use_fd_rejects_negative);
  CU_add_test(s, "do_use_fd: rejects fd >= MAX_CLIENTS",
              test_do_use_fd_rejects_gte_max);
  CU_add_test(s, "do_use_fd: EOF clears write buffer",
              test_do_use_fd_eof_clears_buffer);
  CU_add_test(s, "do_use_fd: queues received data", test_do_use_fd_queues_data);
  CU_add_test(s, "do_use_fd: clamps overflow to BUF_SIZE",
              test_do_use_fd_clamps_overflow);
  CU_add_test(s, "do_use_fd: accumulates multiple reads",
              test_do_use_fd_accumulates_multiple_writes);

  /* do_write_fd */
  CU_add_test(s, "do_write_fd: rejects fd < 0",
              test_do_write_fd_rejects_negative);
  CU_add_test(s, "do_write_fd: rejects fd >= MAX_CLIENTS",
              test_do_write_fd_rejects_gte_max);
  CU_add_test(s, "do_write_fd: sends buffered data",
              test_do_write_fd_sends_data);
  CU_add_test(s, "do_write_fd: empty buffer is noop",
              test_do_write_fd_empty_buffer_noop);
  CU_add_test(s, "do_write_fd: disarms EPOLLOUT on drain",
              test_do_write_fd_disarms_epollout_when_drained);

  /* round-trip */
  CU_add_test(s, "round-trip echo", test_roundtrip_echo);

  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  unsigned int failures = CU_get_number_of_failures();
  CU_cleanup_registry();
  return failures > 0 ? 1 : 0;
}
