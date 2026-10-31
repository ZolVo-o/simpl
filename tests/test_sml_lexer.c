#include "../src/sml_lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_sample_tokens(void)
{
    const char source[] =
        "окно заголовок=\"Тест\"\n"
        "    кнопка id=\"ок\" \"OK\"\n";
    const SmlTokenType expected[] = {
        SML_TOKEN_TAG, SML_TOKEN_ATTRIBUTE, SML_TOKEN_EQUAL, SML_TOKEN_STRING,
        SML_TOKEN_NEWLINE, SML_TOKEN_INDENT, SML_TOKEN_TAG,
        SML_TOKEN_ATTRIBUTE, SML_TOKEN_EQUAL, SML_TOKEN_STRING, SML_TOKEN_STRING,
        SML_TOKEN_NEWLINE, SML_TOKEN_DEDENT, SML_TOKEN_EOF
    };
    SmlLexer lexer;
    size_t i;

    sml_lexer_init(&lexer, source, strlen(source));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        SmlToken token = sml_lexer_next(&lexer);
        assert(token.type == expected[i]);
        if (i == 0) assert(token.line == 1 && token.column == 1);
        if (i == 5) assert(token.line == 2 && token.column == 1);
    }
}

static void test_tag_and_attribute_words(void)
{
    const char source[] =
        "окно\n"
        "    картинка путь=\"logo.png\" ширина=24 высота=12\n"
        "конец окна\n";
    SmlLexer lexer;
    SmlToken token;
    int saw_end = 0, saw_path = 0, saw_width = 0, saw_height = 0;

    sml_lexer_init(&lexer, source, strlen(source));
    while ((token = sml_lexer_next(&lexer)).type != SML_TOKEN_EOF) {
        if (token.type == SML_TOKEN_ERROR) {
            fprintf(stderr, "Лексическая ошибка в строке %lu: %s\n",
                    (unsigned long)token.line, token.error);
            assert(0);
        }
        if (token.type == SML_TOKEN_END) saw_end = 1;
        if (token.type == SML_TOKEN_ATTRIBUTE
            && token.length == sizeof("путь") - 1) saw_path = 1;
        if (token.type == SML_TOKEN_ATTRIBUTE
            && token.length == sizeof("ширина") - 1) saw_width = 1;
        if (token.type == SML_TOKEN_ATTRIBUTE
            && token.length == sizeof("высота") - 1) saw_height = 1;
        if (token.type == SML_TOKEN_NUMBER) assert(token.number == 24.0 || token.number == 12.0);
    }
    assert(saw_end && saw_path && saw_width && saw_height);
}

static void test_comments_empty_lines_and_eof_dedents(void)
{
    const char source[] = "## docs\nокно\n\n    текст \"x\" # inline\n";
    const SmlTokenType expected[] = {
        SML_TOKEN_TAG, SML_TOKEN_NEWLINE, SML_TOKEN_INDENT, SML_TOKEN_TAG,
        SML_TOKEN_STRING, SML_TOKEN_NEWLINE, SML_TOKEN_DEDENT, SML_TOKEN_EOF
    };
    SmlLexer lexer;
    size_t i;

    sml_lexer_init(&lexer, source, strlen(source));
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        assert(sml_lexer_next(&lexer).type == expected[i]);

    sml_lexer_init(&lexer, "# final comment", sizeof("# final comment") - 1);
    assert(sml_lexer_next(&lexer).type == SML_TOKEN_EOF);
}

static void expect_lex_error(const char *source, const char *message)
{
    SmlLexer lexer;
    SmlToken token;
    sml_lexer_init(&lexer, source, strlen(source));
    do {
        token = sml_lexer_next(&lexer);
    } while (token.type != SML_TOKEN_ERROR && token.type != SML_TOKEN_EOF);
    assert(token.type == SML_TOKEN_ERROR);
    assert(token.error != NULL);
    assert(strstr(token.error, message) != NULL);
}

static void test_lexical_errors(void)
{
    expect_lex_error("окно\n  текст \"x\"\n", "кратен четырём");
    expect_lex_error("окно\n        текст \"x\"\n    текст \"y\"\n",
                     "не совпадает");
    expect_lex_error("окно заголовок=\"без конца\n", "несколько строк");
    expect_lex_error("окно @\n", "Неизвестный символ");
}

int main(void)
{
    test_sample_tokens();
    test_tag_and_attribute_words();
    test_comments_empty_lines_and_eof_dedents();
    test_lexical_errors();
    puts("Тесты лексера SML пройдены.");
    return 0;
}
