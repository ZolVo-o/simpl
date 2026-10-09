#include "../src/parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_arithmetic_precedence(void)
{
    const char source[] = "пусть x = 5 + 3 * 2\n";
    Parser parser;
    Node *program;
    Node *value;

    parser_init(&parser, source, strlen(source));
    program = parser_parse(&parser);
    assert(program != NULL);
    assert(parser.error == NULL);
    assert(program->type == NODE_PROGRAM && program->as.program.count == 1);
    value = program->as.program.items[0]->as.declaration.value;
    assert(value->type == NODE_BINOP && value->as.binop.op == TOKEN_PLUS);
    assert(value->as.binop.right->type == NODE_BINOP);
    assert(value->as.binop.right->as.binop.op == TOKEN_STAR);
    ast_print(program);
    putchar('\n');
    ast_free(program);
}

static void test_assignment_and_say(void)
{
    const char source[] = "x = 10\nсказать x\n";
    Parser parser;
    Node *program;

    parser_init(&parser, source, strlen(source));
    program = parser_parse(&parser);
    assert(program != NULL);
    assert(program->as.program.count == 2);
    assert(program->as.program.items[0]->type == NODE_ASSIGN);
    assert(program->as.program.items[1]->type == NODE_SAY);
    ast_free(program);
}

static void test_parse_error(void)
{
    const char source[] = "пусть = 2";
    Parser parser;

    parser_init(&parser, source, strlen(source));
    assert(parser_parse(&parser) == NULL);
    assert(parser.error != NULL);
    assert(parser.error_line == 1);
}

static void test_nested_blocks(void)
{
    const char source[] =
        "пусть root = 1\n"
        "    пусть child = 2\n"
        "        сказать child\n";
    Parser parser;
    Node *program;
    Node *outer;
    Node *inner;

    parser_init(&parser, source, strlen(source));
    program = parser_parse(&parser);
    assert(program != NULL);
    assert(program->as.program.count == 2);
    outer = program->as.program.items[1];
    assert(outer->type == NODE_BLOCK && outer->as.program.count == 2);
    inner = outer->as.program.items[1];
    assert(inner->type == NODE_BLOCK && inner->as.program.count == 1);
    assert(inner->as.program.items[0]->type == NODE_SAY);
    ast_free(program);
}

int main(void)
{
    test_arithmetic_precedence();
    test_assignment_and_say();
    test_parse_error();
    test_nested_blocks();
    puts("Тесты парсера пройдены.");
    return 0;
}
