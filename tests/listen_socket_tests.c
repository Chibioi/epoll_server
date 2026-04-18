#include "../src/epoll.h"
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
#include <unistd.h>

void test_listen_socket_does_not_crash(void) {
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    set_socket_opt(fd);
    bind_socket(fd, 9104);
    listen_socket(fd);
    CU_PASS("listen_socket completed without crashing");
    close(fd);
  }
}

void test_listen_socket_without_bind_fails(void) {
  // listen() on an unbound socket should fail
  int fd = create_socket();
  CU_ASSERT_TRUE(fd >= 0);
  if (fd >= 0) {
    int result = listen_socket(fd);
    CU_ASSERT_TRUE(result < 0);
    close(fd);
  }
}

int main(void) {
  if (CU_initialize_registry() != CUE_SUCCESS)
    return CU_get_error();

  CU_pSuite suite = CU_add_suite("listen_socket", NULL, NULL);
  if (!suite) {
    CU_cleanup_registry();
    return CU_get_error();
  }

  CU_add_test(suite, "does not crash after bind",
              test_listen_socket_does_not_crash);
  CU_add_test(suite, "unbound socket listen fails",
              test_listen_socket_without_bind_fails);

  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  unsigned int failures = CU_get_number_of_failures();
  CU_cleanup_registry();
  return (int)failures;
}
