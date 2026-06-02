# Buccaneer Test Suite Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I.. -D_POSIX_C_SOURCE=200809L -g

BUILD_DIR = ../build
LIB = $(BUILD_DIR)/libbuccvm.a

TESTS = test_value test_lexer test_parser test_package test_vm test_e2e

.PHONY: all clean run

all: $(TESTS)

test_value: test_value.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

test_lexer: test_lexer.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

test_parser: test_parser.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

test_package: test_package.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

test_vm: test_vm.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

test_e2e: test_e2e.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) -lm

run: all
	@echo ""
	@echo "Running all tests..."
	@echo ""
	@failed=0; \
	for test in $(TESTS); do \
		echo "----------------------------------------"; \
		./$$test || failed=$$((failed + 1)); \
	done; \
	echo "----------------------------------------"; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
		echo "All test suites passed!"; \
	else \
		echo "$$failed test suite(s) had failures."; \
		exit 1; \
	fi

clean:
	rm -f $(TESTS)
