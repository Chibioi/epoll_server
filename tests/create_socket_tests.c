#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/epoll.h"

static int fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

void test_create_socket_returns_valid_fd(void) {
    int fd = create_socket();
    CU_ASSERT_TRUE(fd >= 0);
    CU_ASSERT_TRUE(fd_is_valid(fd));
    if (fd >= 0) close(fd);
}

void test_create_socket_two_calls_give_distinct_fds(void) {
    int fd1 = create_socket();
    int fd2 = create_socket();
    CU_ASSERT_TRUE(fd1 >= 0);
    CU_ASSERT_TRUE(fd2 >= 0);
    CU_ASSERT_NOT_EQUAL(fd1, fd2);
    if (fd1 >= 0) close(fd1);
    if (fd2 >= 0) close(fd2);
}

void test_create_socket_fd_invalid_after_close(void) {
    int fd = create_socket();
    CU_ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        close(fd);
        CU_ASSERT_FALSE(fd_is_valid(fd));
    }
}

int main(void) {
    if (CU_initialize_registry() != CUE_SUCCESS)
        return CU_get_error();

    CU_pSuite suite = CU_add_suite("create_socket", NULL, NULL);
    if (!suite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_add_test(suite, "returns a valid fd",           test_create_socket_returns_valid_fd);
    CU_add_test(suite, "two calls give distinct fds",  test_create_socket_two_calls_give_distinct_fds);
    CU_add_test(suite, "fd is invalid after close",    test_create_socket_fd_invalid_after_close);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    unsigned int failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;   // non-zero exit lets Make detect failures
}
