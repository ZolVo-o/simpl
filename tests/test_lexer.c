#include "../src/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_let_example(void)
{
    const char source[] = "пусть x = 5";
    const TokenType expected[] = {
        TOKEN_LET, TOKEN_IDENTIFIER, TOKEN_EQUAL, TOKEN_NUMBER, TOKEN_EOF
    };
    Lexer lexer;
    size_t i;

    lexer_init(&lexer, source, strlen(source));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        Token token = lexer_next(&lexer);
        assert(token.type == expected[i]);
        if (token.type == TOKEN_NUMBER) {
            assert(token.number == 5.0);
        }
    }
}

static void test_lines_comments_and_operators(void)
{
    const char source[] = "# комментарий\nесли x >= 10 и не ложь_\n";
    Lexer lexer;
    Token token;

    lexer_init(&lexer, source, strlen(source));
    token = lexer_next(&lexer);
    assert(token.type == TOKEN_NEWLINE && token.line == 1);
    token = lexer_next(&lexer);
    assert(token.type == TOKEN_IF && token.line == 2);
    assert(lexer_next(&lexer).type == TOKEN_IDENTIFIER);
    assert(lexer_next(&lexer).type == TOKEN_GREATER_EQUAL);
    assert(lexer_next(&lexer).type == TOKEN_NUMBER);
    assert(lexer_next(&lexer).type == TOKEN_AND);
    assert(lexer_next(&lexer).type == TOKEN_NOT);
    assert(lexer_next(&lexer).type == TOKEN_IDENTIFIER);
    assert(lexer_next(&lexer).type == TOKEN_NEWLINE);
    assert(lexer_next(&lexer).type == TOKEN_EOF);
}

static void test_unterminated_string(void)
{
    const char source[] = "\"текст";
    Lexer lexer;
    Token token;

    lexer_init(&lexer, source, strlen(source));
    token = lexer_next(&lexer);
    assert(token.type == TOKEN_ERROR);
    assert(token.error != NULL);
}

static void test_indentation_tokens(void)
{
    const char source[] = "a\n  b\n    c\n  d\ne\n";
    const TokenType expected[] = {
        TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_INDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE,
        TOKEN_DEDENT, TOKEN_IDENTIFIER, TOKEN_NEWLINE, TOKEN_EOF
    };
    Lexer lexer;
    size_t i;

    lexer_init(&lexer, source, strlen(source));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        Token token = lexer_next(&lexer);
        assert(token.type == expected[i]);
    }
}

static void test_invalid_dedent(void)
{
    const char source[] = "a\n    b\n  c";
    Lexer lexer;
    Token token;

    lexer_init(&lexer, source, strlen(source));
    do {
        token = lexer_next(&lexer);
    } while (token.type != TOKEN_ERROR && token.type != TOKEN_EOF);
    assert(token.type == TOKEN_ERROR);
    assert(token.error != NULL);
}

int main(void)
{
    test_let_example();
    test_lines_comments_and_operators();
    test_unterminated_string();
    test_indentation_tokens();
    test_invalid_dedent();
    puts("Тесты лексера пройдены.");
    return 0;
}
