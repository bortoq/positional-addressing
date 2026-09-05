CC      ?= cc
CFLAGS  ?= -Wall -Wextra -pedantic -std=c11 -O2
SPACE_SIZE ?= 65520

SRC_DIR   = src
VM1_SRC   = $(SRC_DIR)/machine.c
SORT_SRC = $(SRC_DIR)/network.c
MKLDR1_SRC = tools/loader.c
C3_SRC    = tools/compile.c
TEST_SORT_SRC = tests/test_network.c
TEST_COMPAT_SRC = tests/test_channel.c
TEST_MACHINE_SRC = tests/test_machine.c
TEST_NETUNIT_SRC = tests/test_network_unit.c
TEST_LOADER_SRC = tests/test_loader.c
TEST_COMPILE_SRC = tests/test_compile.c

VM1_BIN   = vm1
SORT_BIN = sort
MKLDR1_BIN = tools/loader
C3_BIN    = tools/compile
TEST_SORT_BIN = tests/test_network
TEST_COMPAT_BIN = tests/test_channel
TEST_MACHINE_BIN = tests/test_machine
TEST_NETUNIT_BIN = tests/test_network_unit
TEST_LOADER_BIN = tests/test_loader
TEST_COMPILE_BIN = tests/test_compile
SORTER    = sorter.txt

.PHONY: all clean sorter check test help tools tests

all: $(VM1_BIN) $(SORT_BIN) tools

tools: $(MKLDR1_BIN) $(C3_BIN)

tests: $(TEST_SORT_BIN) $(TEST_COMPAT_BIN) $(TEST_MACHINE_BIN) $(TEST_NETUNIT_BIN) $(TEST_LOADER_BIN) $(TEST_COMPILE_BIN)

$(VM1_BIN): $(VM1_SRC) $(SRC_DIR)/machine.h $(SRC_DIR)/loader.h
	$(CC) $(CFLAGS) $(VM1_SRC) -o $@

$(SORT_BIN): $(SORT_SRC)
	$(CC) $(CFLAGS) $(SORT_SRC) -o $@

$(MKLDR1_BIN): $(MKLDR1_SRC)
	$(CC) $(CFLAGS) $(MKLDR1_SRC) -o $@

$(C3_BIN): $(C3_SRC)
	$(CC) $(CFLAGS) $(C3_SRC) -o $@

$(TEST_SORT_BIN): $(TEST_SORT_SRC)
	$(CC) $(CFLAGS) $(TEST_SORT_SRC) -o $@

$(TEST_COMPAT_BIN): $(TEST_COMPAT_SRC)
	$(CC) $(CFLAGS) $(TEST_COMPAT_SRC) -o $@

$(TEST_MACHINE_BIN): $(TEST_MACHINE_SRC) $(SRC_DIR)/machine.c $(SRC_DIR)/machine.h $(SRC_DIR)/loader.h
	$(CC) $(CFLAGS) -DUNIT_TEST $(TEST_MACHINE_SRC) -o $@

$(TEST_NETUNIT_BIN): $(TEST_NETUNIT_SRC) $(SRC_DIR)/network.c
	$(CC) $(CFLAGS) -DUNIT_TEST $(TEST_NETUNIT_SRC) -o $@

$(TEST_LOADER_BIN): $(TEST_LOADER_SRC) tools/loader.c
	$(CC) $(CFLAGS) -DUNIT_TEST $(TEST_LOADER_SRC) -o $@

$(TEST_COMPILE_BIN): $(TEST_COMPILE_SRC) tools/compile.c
	$(CC) $(CFLAGS) -DUNIT_TEST $(TEST_COMPILE_SRC) -o $@

# Generate default sorting network
sorter: $(SORTER)

$(SORTER): $(SORT_BIN)
	./$(SORT_BIN) $(SPACE_SIZE) > $@

# Run all project tests (C only, no python)
check: all $(SORTER) tests
	@echo "== machine unit (per-function) =="
	./$(TEST_MACHINE_BIN)
	@echo "== network unit (per-function) =="
	./$(TEST_NETUNIT_BIN)
	@echo "== loader unit (per-function) =="
	./$(TEST_LOADER_BIN)
	@echo "== compile unit (per-function) =="
	./$(TEST_COMPILE_BIN)
	@echo "== sort: 256-pair sanity =="
	./$(TEST_SORT_BIN)
	@echo "== vm1 channel compat (pairwise stable, 2-phase) =="
	./$(TEST_COMPAT_BIN)

test: check

clean:
	rm -f $(VM1_BIN) $(SORT_BIN) $(SORTER) $(MKLDR1_BIN) $(C3_BIN) $(TEST_SORT_BIN) $(TEST_COMPAT_BIN) $(TEST_MACHINE_BIN) $(TEST_NETUNIT_BIN) $(TEST_LOADER_BIN) $(TEST_COMPILE_BIN)
	rm -f tmp_* tmp-*

help:
	@echo "Targets:"
	@echo "  all     - build vm1, sort and tools"
	@echo "  tools   - build loader and compile"
	@echo "  tests   - build C test binaries"
	@echo "  sorter  - generate sorter.txt for SPACE_SIZE=$(SPACE_SIZE)"
	@echo "  check   - run C tests"
	@echo "  clean   - remove build artifacts"
	@echo "  help    - this message"
