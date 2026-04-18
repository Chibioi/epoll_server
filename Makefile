CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -lcunit
SRC     = epoll.c

# Automatically find all test source files and derive binary names from them
TEST_SRCS := $(wildcard tests/test_*.c)
TESTS     := $(TEST_SRCS:.c=)

all: $(TESTS)

# Pattern rule — applies to every tests/test_* binary
tests/%: tests/%.c $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Run all test binaries in one shot
run: all
	@for t in $(TESTS); do \
		echo "=============================="; \
		echo " Running: $$t"; \
		echo "=============================="; \
		./$$t || exit 1; \
	done
	@echo ""
	@echo "All test suites passed!"

clean:
	rm -f $(TESTS)
