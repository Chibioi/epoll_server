/*
 * test_register_socket.c
 *
 * CUnit tests for:
 *   int register_socket(int sockfd, int epoll_fd);
 *
 * Build (adjust paths as needed):
 *   gcc -Wall -o test_register_socket test_register_socket.c \
 *       -lcunit -Wl,--wrap=epoll_ctl -Wl,--wrap=exit
 *
 * Run:
 *   ./test_register_socket
 */

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include "../src/epoll.h"

/* -----------------------------------------------------------------------
 * Linker-wrap stubs for epoll_ctl() and exit()
 * -----------------------------------------------------------------------
 * Compiled with -Wl,--wrap=epoll_ctl  -Wl,--wrap=exit
 * so every call to epoll_ctl / exit inside the TU under test is redirected
 * here, letting us control return values without touching the OS.
 * ----------------------------------------------------------------------- */

/* --- epoll_ctl mock ------------------------------------------------------- */
static int   g_epoll_ctl_retval  = 0;   /* value the mock returns          */
static int   g_epoll_ctl_called  = 0;   /* call counter                    */
static int   g_epoll_ctl_epfd    = -1;  /* captured arguments              */
static int   g_epoll_ctl_op      = -1;
static int   g_epoll_ctl_fd      = -1;
static struct epoll_event g_epoll_ctl_event; /* captured event struct        */

int __wrap_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    g_epoll_ctl_called++;
    g_epoll_ctl_epfd = epfd;
    g_epoll_ctl_op   = op;
    g_epoll_ctl_fd   = fd;
    if (event) g_epoll_ctl_event = *event;
    errno = 0;
    if (g_epoll_ctl_retval < 0) errno = EBADF; /* simulate a plausible errno */
    return g_epoll_ctl_retval;
}

/* --- exit() mock ---------------------------------------------------------- */
static int g_exit_called    = 0;
static int g_exit_status    = -999;

/* exit() is declared noreturn; we suppress that for testing purposes.       */
void __wrap_exit(int status) {
    g_exit_called++;
    g_exit_status = status;
    /* Do NOT actually exit – just return so the test can inspect state.     */
}

static void reset_mocks(void) {
    g_epoll_ctl_retval = 0;
    g_epoll_ctl_called = 0;
    g_epoll_ctl_epfd   = -1;
    g_epoll_ctl_op     = -1;
    g_epoll_ctl_fd     = -1;
    memset(&g_epoll_ctl_event, 0, sizeof g_epoll_ctl_event);

    g_exit_called = 0;
    g_exit_status = -999;
}

/* -----------------------------------------------------------------------
 * Test cases
 * ----------------------------------------------------------------------- */

/*
 * TC-01 – Happy path: epoll_ctl succeeds (returns 0).
 *         register_socket must return 0 and must NOT call exit().
 */
static void test_success_returns_zero(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    int result = register_socket(5, 10);

    CU_ASSERT_EQUAL(result, 0);
    CU_ASSERT_EQUAL(g_exit_called, 0);
}

/*
 * TC-02 – epoll_ctl is called exactly once per register_socket call.
 */
static void test_epoll_ctl_called_once(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(5, 10);

    CU_ASSERT_EQUAL(g_epoll_ctl_called, 1);
}

/*
 * TC-03 – The epoll file descriptor is forwarded correctly.
 */
static void test_epoll_fd_forwarded(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(5, 10);

    CU_ASSERT_EQUAL(g_epoll_ctl_epfd, 10);
}

/*
 * TC-04 – The socket fd is forwarded correctly as both the watched fd
 *          and stored inside event.data.fd.
 */
static void test_sockfd_forwarded(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(5, 10);

    CU_ASSERT_EQUAL(g_epoll_ctl_fd, 5);
    CU_ASSERT_EQUAL(g_epoll_ctl_event.data.fd, 5);
}

/*
 * TC-05 – The operation passed to epoll_ctl must be EPOLL_CTL_ADD.
 */
static void test_operation_is_epoll_ctl_add(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(5, 10);

    CU_ASSERT_EQUAL(g_epoll_ctl_op, EPOLL_CTL_ADD);
}

/*
 * TC-06 – The event mask must include EPOLLIN.
 */
static void test_event_mask_epollin(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(5, 10);

    CU_ASSERT(g_epoll_ctl_event.events & EPOLLIN);
}

/*
 * TC-07 – Failure path: epoll_ctl returns -1.
 *         register_socket must call exit(1) exactly once.
 */
static void test_failure_calls_exit(void) {
    reset_mocks();
    g_epoll_ctl_retval = -1;

    register_socket(5, 10);   /* exit() is mocked; won't actually terminate */

    CU_ASSERT_EQUAL(g_exit_called, 1);
    CU_ASSERT_EQUAL(g_exit_status, 1);
}

/*
 * TC-08 – Boundary: sockfd == 0 (stdin fd, unusual but valid).
 */
static void test_sockfd_zero(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    int result = register_socket(0, 10);

    CU_ASSERT_EQUAL(result, 0);
    CU_ASSERT_EQUAL(g_epoll_ctl_fd, 0);
    CU_ASSERT_EQUAL(g_epoll_ctl_event.data.fd, 0);
}

/*
 * TC-09 – Boundary: large fd values (e.g. INT_MAX-1).
 */
static void test_large_fd_values(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;
    int big_sock  = 1023;
    int big_epoll = 1022;

    int result = register_socket(big_sock, big_epoll);

    CU_ASSERT_EQUAL(result, 0);
    CU_ASSERT_EQUAL(g_epoll_ctl_epfd, big_epoll);
    CU_ASSERT_EQUAL(g_epoll_ctl_fd,   big_sock);
}

/*
 * TC-10 – Multiple sequential calls each invoke epoll_ctl independently.
 */
static void test_multiple_calls_independent(void) {
    reset_mocks();
    g_epoll_ctl_retval = 0;

    register_socket(3, 10);
    register_socket(4, 10);
    register_socket(5, 10);

    CU_ASSERT_EQUAL(g_epoll_ctl_called, 3);
    /* Last captured fd should be from the last call */
    CU_ASSERT_EQUAL(g_epoll_ctl_fd, 5);
}

/* -----------------------------------------------------------------------
 * Suite setup / teardown  (nothing OS-level to acquire / release here)
 * ----------------------------------------------------------------------- */
static int suite_init(void)    { return 0; }
static int suite_cleanup(void) { return 0; }

/* -----------------------------------------------------------------------
 * main – register suite and run
 * ----------------------------------------------------------------------- */
int main(void) {
    if (CU_initialize_registry() != CUE_SUCCESS)
        return (int)CU_get_error();

    CU_pSuite suite = CU_add_suite("register_socket", suite_init, suite_cleanup);
    if (!suite) {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    /* Register every test */
    CU_add_test(suite, "success: returns 0",               test_success_returns_zero);
    CU_add_test(suite, "epoll_ctl called exactly once",    test_epoll_ctl_called_once);
    CU_add_test(suite, "epoll fd forwarded correctly",     test_epoll_fd_forwarded);
    CU_add_test(suite, "sockfd forwarded correctly",       test_sockfd_forwarded);
    CU_add_test(suite, "operation is EPOLL_CTL_ADD",       test_operation_is_epoll_ctl_add);
    CU_add_test(suite, "event mask includes EPOLLIN",      test_event_mask_epollin);
    CU_add_test(suite, "failure: exit(1) called",          test_failure_calls_exit);
    CU_add_test(suite, "boundary: sockfd == 0",            test_sockfd_zero);
    CU_add_test(suite, "boundary: large fd values",        test_large_fd_values);
    CU_add_test(suite, "multiple sequential calls",        test_multiple_calls_independent);

    /* Run in verbose mode */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    unsigned int failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;   /* 0 = all passed */
}


