#include "../src/epoll.h"
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <unistd.h>

void test_set_socket_opt_succeeds_on_valid_fd(void) {
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    int result = set_socket_opt(fd);
    CU_ASSERT_EQUAL(result, 0);
    close(fd);
  }
}

void test_set_socket_opt_fails_on_invalid_fd(void) {
  int result = set_socket_opt(-1);
  CU_ASSERT_TRUE(result < 0);
}

void test_set_socket_opt_idempotent(void) {
  // Calling it twice on the same fd must not fail the second time
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    CU_ASSERT_EQUAL(set_socket_opt(fd), 0);
    CU_ASSERT_EQUAL(set_socket_opt(fd), 0);
    close(fd);
  }
}

int main(void) {
  if (CU_initialize_registry() != CUE_SUCCESS)
    return CU_get_error();

  CU_pSuite suite = CU_add_suite("set_socket_opt", NULL, NULL);
  if (!suite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  CU_add_test(suite, "succeeds on valid fd",
              test_set_socket_opt_succeeds_on_valid_fd);
  CU_add_test(suite, "fails on invalid fd",
              test_set_socket_opt_fails_on_invalid_fd);
  CU_add_test(suite, "idempotent on same fd", test_set_socket_opt_idempotent);

  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  unsigned int failures = CU_get_number_of_failures();
  CU_cleanup_registry();
  return (int)failures;
}
