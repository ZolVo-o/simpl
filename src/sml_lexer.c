#include "sml_lexer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t length;
    SmlTokenType type;
} SmlWord;

static const SmlWord tags[] = {
    {"окно", sizeof("окно") - 1, SML_TOKEN_TAG},
    {"строка", sizeof("строка") - 1, SML_TOKEN_TAG},
    {"столбец", sizeof("столбец") - 1, SML_TOKEN_TAG},
    {"текст", sizeof("текст") - 1, SML_TOKEN_TAG},
    {"кнопка", sizeof("кнопка") - 1, SML_TOKEN_TAG},
    {"поле", sizeof("поле") - 1, SML_TOKEN_TAG},
    {"картинка", sizeof("картинка") - 1, SML_TOKEN_TAG},
    {"конец", sizeof("конец") - 1, SML_TOKEN_END}
};

static const SmlWord attributes[] = {
    {"id", sizeof("id") - 1, SML_TOKEN_ATTRIBUTE},
    {"заголовок", sizeof("заголовок") - 1, SML_TOKEN_ATTRIBUTE},
    {"ширина", sizeof("ширина") - 1, SML_TOKEN_ATTRIBUTE},
    {"высота", sizeof("высота") - 1, SML_TOKEN_ATTRIBUTE},
    {"отступ", sizeof("отступ") - 1, SML_TOKEN_ATTRIBUTE},
    {"расстояние", sizeof("расстояние") - 1, SML_TOKEN_ATTRIBUTE},
    {"подсказка", sizeof("подсказка") - 1, SML_TOKEN_ATTRIBUTE},
    {"значение", sizeof("значение") - 1, SML_TOKEN_ATTRIBUTE},
    {"путь", sizeof("путь") - 1, SML_TOKEN_ATTRIBUTE}
};

static int at_end(const SmlLexer *lexer)
{
    return lexer->position >= lexer->length;
}

static unsigned char peek(const SmlLexer *lexer)
{
    return at_end(lexer) ? '\0' : (unsigned char)lexer->source[lexer->position];
}

static unsigned char advance(SmlLexer *lexer)
{
    unsigned char c = (unsigned char)lexer->source[lexer->position++];
    if (c == '\n') {
        ++lexer->line;
        lexer->column = 1;
    } else {
        ++lexer->column;
    }
    return c;
}

static SmlToken make_token(const SmlLexer *lexer, SmlTokenType type,
                           size_t start, size_t line, size_t column)
{
    SmlToken token;
    token.type = type;
    token.start = lexer->source + start;
    token.length = lexer->position - start;
    token.line = line;
    token.column = column;
    token.number = 0.0;
    token.error = NULL;
    return token;
}

static SmlToken error_token(SmlLexer *lexer, size_t start, size_t line,
                            size_t column, const char *message)
{
    SmlToken token = make_token(lexer, SML_TOKEN_ERROR, start, line, column);
    token.error = message;
    lexer->failed = 1;
    return token;
}

static SmlTokenType word_type(const char *word, size_t length)
{
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        if (tags[i].length == length && memcmp(tags[i].text, word, length) == 0)
            return tags[i].type;
    }
    for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
        if (attributes[i].length == length
            && memcmp(attributes[i].text, word, length) == 0)
            return attributes[i].type;
    }
    return SML_TOKEN_IDENTIFIER;
}

static int is_word_byte(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

void sml_lexer_init(SmlLexer *lexer, const char *source, size_t length)
{
    lexer->source = source;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->indent_levels[0] = 0;
    lexer->indent_count = 1;
    lexer->pending_dedents = 0;
    lexer->at_line_start = 1;
    lexer->line_has_tokens = 0;
    lexer->eof_newline_emitted = 0;
    lexer->failed = 0;
}

SmlToken sml_lexer_next(SmlLexer *lexer)
{
    size_t start, line, column;
    unsigned char c;
    SmlToken token;

    if (lexer->failed)
        return make_token(lexer, SML_TOKEN_EOF, lexer->position,
                          lexer->line, lexer->column);

    if (lexer->pending_dedents > 0) {
        --lexer->pending_dedents;
        return make_token(lexer, SML_TOKEN_DEDENT, lexer->position,
                          lexer->line, lexer->column);
    }

next_token:
    if (lexer->at_line_start) {
        size_t indent = 0;
        start = lexer->position;
        line = lexer->line;
        column = lexer->column;
        while (!at_end(lexer) && (peek(lexer) == ' ' || peek(lexer) == '\t'))
            indent += advance(lexer) == '\t' ? 4 : 1;

        if (at_end(lexer)) {
            if (lexer->indent_count > 1) {
                --lexer->indent_count;
                return make_token(lexer, SML_TOKEN_DEDENT, lexer->position,
                                  lexer->line, lexer->column);
            }
            return make_token(lexer, SML_TOKEN_EOF, lexer->position,
                              lexer->line, lexer->column);
        }
        if (peek(lexer) == '\n' || peek(lexer) == '\r' || peek(lexer) == '#') {
            while (!at_end(lexer) && peek(lexer) != '\n') advance(lexer);
            if (!at_end(lexer)) advance(lexer);
            lexer->at_line_start = 1;
            lexer->line_has_tokens = 0;
            goto next_token;
        }
        if (indent % 4 != 0)
            return error_token(lexer, start, line, column,
                              "Отступ должен быть кратен четырём пробелам");
        lexer->at_line_start = 0;
        if (indent > lexer->indent_levels[lexer->indent_count - 1]) {
            if (lexer->indent_count == sizeof(lexer->indent_levels)
                                         / sizeof(lexer->indent_levels[0]))
                return error_token(lexer, start, line, column,
                                   "Слишком много уровней отступа");
            lexer->indent_levels[lexer->indent_count++] = indent;
            return make_token(lexer, SML_TOKEN_INDENT, start, line, column);
        }
        if (indent < lexer->indent_levels[lexer->indent_count - 1]) {
            while (lexer->indent_count > 1
                   && indent < lexer->indent_levels[lexer->indent_count - 1]) {
                --lexer->indent_count;
                ++lexer->pending_dedents;
            }
            if (indent != lexer->indent_levels[lexer->indent_count - 1])
                return error_token(lexer, start, line, column,
                                   "Отступ не совпадает с предыдущим уровнем");
            --lexer->pending_dedents;
            return make_token(lexer, SML_TOKEN_DEDENT, start, line, column);
        }
    }

    for (;;) {
        if (at_end(lexer)) {
            if (lexer->line_has_tokens && !lexer->eof_newline_emitted) {
                lexer->line_has_tokens = 0;
                lexer->eof_newline_emitted = 1;
                return make_token(lexer, SML_TOKEN_NEWLINE, lexer->position,
                                  lexer->line, lexer->column);
            }
            if (lexer->indent_count > 1) {
                --lexer->indent_count;
                return make_token(lexer, SML_TOKEN_DEDENT, lexer->position,
                                  lexer->line, lexer->column);
            }
            return make_token(lexer, SML_TOKEN_EOF, lexer->position,
                              lexer->line, lexer->column);
        }
        c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
        } else if (c == '#') {
            while (!at_end(lexer) && peek(lexer) != '\n') advance(lexer);
        } else {
            break;
        }
    }

    start = lexer->position;
    line = lexer->line;
    column = lexer->column;
    c = advance(lexer);
    if (c == '\n') {
        lexer->at_line_start = 1;
        if (!lexer->line_has_tokens) goto next_token;
        lexer->line_has_tokens = 0;
        return make_token(lexer, SML_TOKEN_NEWLINE, start, line, column);
    }
    if (c == '=') {
        lexer->line_has_tokens = 1;
        return make_token(lexer, SML_TOKEN_EQUAL, start, line, column);
    }
    if (c == '"' || c == '\'') {
        unsigned char quote = c;
        while (!at_end(lexer) && peek(lexer) != quote) {
            if (peek(lexer) == '\n' || peek(lexer) == '\r')
                return error_token(lexer, start, line, column,
                                   "Строка не может занимать несколько строк");
            if (peek(lexer) == '\\') {
                advance(lexer);
                if (at_end(lexer)) break;
            }
            advance(lexer);
        }
        if (at_end(lexer))
            return error_token(lexer, start, line, column, "Незакрытая строка");
        advance(lexer);
        lexer->line_has_tokens = 1;
        return make_token(lexer, SML_TOKEN_STRING, start, line, column);
    }
    if (c >= '0' && c <= '9') {
        char number_buffer[128];
        while (peek(lexer) >= '0' && peek(lexer) <= '9') advance(lexer);
        if (peek(lexer) == '.' && lexer->position + 1 < lexer->length
            && lexer->source[lexer->position + 1] >= '0'
            && lexer->source[lexer->position + 1] <= '9') {
            advance(lexer);
            while (peek(lexer) >= '0' && peek(lexer) <= '9') advance(lexer);
        }
        token = make_token(lexer, SML_TOKEN_NUMBER, start, line, column);
        if (token.length >= sizeof(number_buffer))
            return error_token(lexer, start, line, column, "Слишком длинное число");
        memcpy(number_buffer, token.start, token.length);
        number_buffer[token.length] = '\0';
        errno = 0;
        token.number = strtod(number_buffer, NULL);
        if (errno == ERANGE)
            return error_token(lexer, start, line, column, "Число слишком велико");
        lexer->line_has_tokens = 1;
        return token;
    }
    if (is_word_byte(c)) {
        while (!at_end(lexer) && is_word_byte(peek(lexer))) advance(lexer);
        token = make_token(lexer, word_type(lexer->source + start,
                                           lexer->position - start),
                           start, line, column);
        lexer->line_has_tokens = 1;
        return token;
    }
    return error_token(lexer, start, line, column, "Неизвестный символ");
}

const char *sml_token_type_name(SmlTokenType type)
{
    static const char *const names[] = {
        "ошибка", "конец файла", "новая строка", "отступ", "конец блока",
        "тег", "конец", "атрибут", "идентификатор", "строка", "число", "="
    };
    if ((size_t)type >= sizeof(names) / sizeof(names[0]))
        return "неизвестный токен";
    return names[type];
}
