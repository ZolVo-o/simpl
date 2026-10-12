CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2 $(shell python3-config --cflags)
LDFLAGS = $(shell python3-config --embed --ldflags)

SRC = src/main.c src/lexer.c src/parser.c src/compiler.c src/chunk.c src/vm.c src/serialize.c src/disasm.c
LEXER_TEST_SRC = tests/test_lexer.c src/lexer.c
PARSER_TEST_SRC = tests/test_parser.c src/parser.c src/lexer.c
E2E_TEST_SRC = tests/test_compile.c src/lexer.c src/parser.c src/compiler.c src/chunk.c src/vm.c src/serialize.c src/disasm.c

.PHONY: all clean test test-lexer test-parser test-vm test-compile test-cli smoke

all: simpl

test: test-lexer test-parser test-vm test-compile test-cli smoke

simpl: $(SRC) src/opcodes.h src/chunk.h src/vm.h src/serialize.h src/disasm.h src/lexer.h src/parser.h src/ast.h src/compiler.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

test-lexer: $(LEXER_TEST_SRC) src/lexer.h tests/test.h
	$(CC) $(CFLAGS) -UNDEBUG -o test_lexer $(LEXER_TEST_SRC)
	./test_lexer

test-parser: $(PARSER_TEST_SRC) src/parser.h src/ast.h tests/test.h
	$(CC) $(CFLAGS) -UNDEBUG -o test_parser $(PARSER_TEST_SRC)
	./test_parser

test-vm: tests/test_vm.c src/lexer.c src/parser.c src/compiler.c src/chunk.c src/vm.c src/lexer.h src/parser.h src/compiler.h src/vm.h tests/test.h
	$(CC) $(CFLAGS) -UNDEBUG -o test_vm tests/test_vm.c src/lexer.c src/parser.c src/compiler.c src/chunk.c src/vm.c $(LDFLAGS)
	./test_vm

test-compile: $(E2E_TEST_SRC) src/compiler.h src/parser.h src/ast.h src/serialize.h src/disasm.h
	$(CC) $(CFLAGS) -UNDEBUG -o test_compile $(E2E_TEST_SRC) $(LDFLAGS)
	./test_compile

test-cli: simpl
	sh tests/test_cli.sh

smoke: simpl
	bash tests/smoke.sh

clean:
	rm -f simpl test_lexer test_parser test_vm test_compile *.simc
