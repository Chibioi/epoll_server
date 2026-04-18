#include "../src/epoll.h"
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <fcntl.h>
#include <unistd.h>

void test_bind_socket_succeeds(void) {
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    set_socket_opt(fd);
    int result = bind_socket(fd, 9100);
    CU_ASSERT_EQUAL(result, 0);
    close(fd);
  }
}

void test_bind_socket_sets_nonblocking(void) {
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    set_socket_opt(fd);
    bind_socket(fd, 9101);

    int flags = fcntl(fd, F_GETFL, 0);
    CU_ASSERT_TRUE(flags & O_NONBLOCK);
    close(fd);
  }
}

void test_bind_socket_same_port_twice_fails(void) {
  int fd1 = create_socket();
  int fd2 = create_socket();
  CU_ASSERT_TRUE(fd1 >= 0);
  CU_ASSERT_TRUE(fd2 >= 0);
  if (fd1 >= 0 && fd2 >= 0) {
    bind_socket(fd1, 9102);
    int result = bind_socket(fd2, 9102); // must fail
    CU_ASSERT_TRUE(result < 0);
  }
  if (fd1 >= 0)
    close(fd1);
  if (fd2 >= 0)
    close(fd2);
}

void test_bind_socket_invalid_fd_fails(void) {
  int result = bind_socket(-1, 9103);
  CU_ASSERT_TRUE(result < 0);
}

int main(void) {
  if (CU_initialize_registry() != CUE_SUCCESS)
    return CU_get_error();

  CU_pSuite suite = CU_add_suite("bind_socket", NULL, NULL);
  if (!suite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  CU_add_test(suite, "succeeds on available port", test_bind_socket_succeeds);
  CU_add_test(suite, "sets O_NONBLOCK flag", test_bind_socket_sets_nonblocking);
  CU_add_test(suite, "duplicate port fails",
              test_bind_socket_same_port_twice_fails);
  CU_add_test(suite, "invalid fd fails", test_bind_socket_invalid_fd_fails);

  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  unsigned int failures = CU_get_number_of_failures();
  CU_cleanup_registry();
  return (int)failures;
}
