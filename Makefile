# ── Makefile ────────────────────────────────────────────────────────────────
#
# Layout expected:
#   src/epoll.h   src/epoll.c   src/main.c
#   tests/tests.c
#
# Targets:
#   make          → build the server binary  (default)
#   make test     → build & run the CUnit test suite
#   make run      → build & start the server
#   make clean    → remove all build artefacts
# ─────────────────────────────────────────────────────────────────────────────

CC      := gcc
CFLAGS  := -Wall -Wextra -pedantic -std=c11
LDFLAGS :=
LIBS    := -lcunit

BUILD   := build
SRC_DIR := src
TST_DIR := tests

# ── artefact names ───────────────────────────────────────────────────────────
SERVER  := $(BUILD)/server
TEST_BIN:= $(BUILD)/test_runner

# ── source lists ─────────────────────────────────────────────────────────────
SRV_SRCS := $(SRC_DIR)/epoll.c $(SRC_DIR)/main.c
TST_SRCS := $(TST_DIR)/tests.c $(SRC_DIR)/epoll.c   # no main.c — tests supply their own main

# ── default target ────────────────────────────────────────────────────────────
.PHONY: all
all: $(SERVER)

# ── build the server ─────────────────────────────────────────────────────────
$(SERVER): $(SRV_SRCS) $(SRC_DIR)/epoll.h | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRV_SRCS)
	@echo "Built $@"

# ── build & run the test suite ───────────────────────────────────────────────
.PHONY: test
test: $(TEST_BIN)
	@echo "Running tests..."
	@./$(TEST_BIN); \
	  STATUS=$$?; \
	  if [ $$STATUS -eq 0 ]; then \
	    echo "All tests passed."; \
	  else \
	    echo "Tests FAILED (exit $$STATUS)."; \
	  fi; \
	  exit $$STATUS

$(TEST_BIN): $(TST_SRCS) $(SRC_DIR)/epoll.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -o $@ $(TST_SRCS) $(LIBS)
	@echo "Built $@"

# ── run the server (builds first) ────────────────────────────────────────────
.PHONY: run
run: $(SERVER)
	@echo "Starting server..."
	./$(SERVER)

# ── create build directory ────────────────────────────────────────────────────
$(BUILD):
	mkdir -p $(BUILD)

# ── clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(BUILD)
	@echo "Cleaned."
