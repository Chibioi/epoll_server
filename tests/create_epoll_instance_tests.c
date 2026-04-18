#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/epoll.h"

static int fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1;
}

void test_create_epoll_returns_valid_fd(void) {
    int epfd = create_epoll_instance();
    CU_ASSERT_TRUE(epfd >= 0);
    CU_ASSERT_TRUE(fd_is_valid(epfd));
    if (epfd >= 0) close(epfd);
}

void test_create_epoll_two_calls_give_distinct_fds(void) {
    int epfd1 = create_epoll_instance();
    int epfd2 = create_epoll_instance();
    CU_ASSERT_TRUE(epfd1 >= 0);
    CU_ASSERT_TRUE(epfd2 >= 0);
    CU_ASSERT_NOT_EQUAL(epfd1, epfd2);
    if (epfd1 >= 0) close(epfd1);
    if (epfd2 >= 0) close(epfd2);
}

void test_create_epoll_fd_invalid_after_close(void) {
    int epfd = create_epoll_instance();
    CU_ASSERT_TRUE(epfd >= 0);
    if (epfd >= 0) {
        close(epfd);
        CU_ASSERT_FALSE(fd_is_valid(epfd));
    }
}

int main(void) {
    if (CU_initialize_registry() != CUE_SUCCESS)
        return CU_get_error();

    CU_pSuite suite = CU_add_suite("create_epoll_instance", NULL, NULL);
    if (!suite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    CU_add_test(suite, "returns a valid fd",           test_create_epoll_returns_valid_fd);
    CU_add_test(suite, "two calls give distinct fds",  test_create_epoll_two_calls_give_distinct_fds);
    CU_add_test(suite, "fd is invalid after close",    test_create_epoll_fd_invalid_after_close);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    unsigned int failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;
}
