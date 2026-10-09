#include "lexer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t length;
    TokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"пусть", sizeof("пусть") - 1, TOKEN_LET},
    {"сказать", sizeof("сказать") - 1, TOKEN_SAY},
    {"спросить", sizeof("спросить") - 1, TOKEN_ASK},
    {"если", sizeof("если") - 1, TOKEN_IF},
    {"иначе", sizeof("иначе") - 1, TOKEN_ELSE},
    {"пока", sizeof("пока") - 1, TOKEN_WHILE},
    {"повторить", sizeof("повторить") - 1, TOKEN_REPEAT},
    {"раз", sizeof("раз") - 1, TOKEN_TIMES},
    {"как", sizeof("как") - 1, TOKEN_AS},
    {"функция", sizeof("функция") - 1, TOKEN_FUNCTION},
    {"вернуть", sizeof("вернуть") - 1, TOKEN_RETURN},
    {"и", sizeof("и") - 1, TOKEN_AND},
    {"или", sizeof("или") - 1, TOKEN_OR},
    {"не", sizeof("не") - 1, TOKEN_NOT},
    {"да", sizeof("да") - 1, TOKEN_TRUE},
    {"нет", sizeof("нет") - 1, TOKEN_FALSE},
    {"для", sizeof("для") - 1, TOKEN_FOR},
    {"каждого", sizeof("каждого") - 1, TOKEN_EACH},
    {"в", sizeof("в") - 1, TOKEN_IN},
    {"попробовать", sizeof("попробовать") - 1, TOKEN_TRY},
    {"поймать", sizeof("поймать") - 1, TOKEN_CATCH}
};

static int at_end(const Lexer *lexer)
{
    return lexer->position >= lexer->length;
}

static unsigned char peek(const Lexer *lexer)
{
    if (at_end(lexer)) {
        return '\0';
    }
    return (unsigned char)lexer->source[lexer->position];
}

static unsigned char peek_next(const Lexer *lexer)
{
    if (lexer->position + 1 >= lexer->length) {
        return '\0';
    }
    return (unsigned char)lexer->source[lexer->position + 1];
}

static unsigned char advance(Lexer *lexer)
{
    return (unsigned char)lexer->source[lexer->position++];
}

static Token make_token(const Lexer *lexer, TokenType type, size_t start,
                        size_t line)
{
    Token token;
    token.type = type;
    token.start = lexer->source + start;
    token.length = lexer->position - start;
    token.line = line;
    token.number = 0.0;
    token.error = NULL;
    return token;
}

static Token error_token(const Lexer *lexer, size_t start, size_t line,
                         const char *message)
{
    Token token = make_token(lexer, TOKEN_ERROR, start, line);
    token.error = message;
    return token;
}

static TokenType identifier_type(const char *start, size_t length)
{
    size_t i;
    for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (keywords[i].length == length
            && memcmp(start, keywords[i].text, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOKEN_IDENTIFIER;
}

static Token scan_number(Lexer *lexer, size_t start, size_t line)
{
    char buffer[128];
    Token token;

    while (peek(lexer) >= '0' && peek(lexer) <= '9') {
        advance(lexer);
    }
    if (peek(lexer) == '.' && peek_next(lexer) >= '0'
        && peek_next(lexer) <= '9') {
        advance(lexer);
        while (peek(lexer) >= '0' && peek(lexer) <= '9') {
            advance(lexer);
        }
    }

    token = make_token(lexer, TOKEN_NUMBER, start, line);
    if (token.length >= sizeof(buffer)) {
        token.type = TOKEN_ERROR;
        token.error = "Слишком длинное число";
        return token;
    }
    memcpy(buffer, token.start, token.length);
    buffer[token.length] = '\0';
    token.number = strtod(buffer, NULL);
    if (token.number > 1.7976931348623157e308
        || token.number < -1.7976931348623157e308) {
        token.type = TOKEN_ERROR;
        token.error = "Число слишком велико";
    }
    return token;
}

void lexer_init(Lexer *lexer, const char *source, size_t length)
{
    lexer->source = source;
    lexer->length = length;
    lexer->position = 0;
    lexer->line = 1;
    lexer->indent_levels[0] = 0;
    lexer->indent_count = 1;
    lexer->pending_dedents = 0;
    lexer->at_line_start = 1;
    lexer->eof_processed = 0;
}

Token lexer_next(Lexer *lexer)
{
    size_t start;
    size_t line;
    unsigned char c;
    Token token;

    if (lexer->pending_dedents > 0) {
        --lexer->pending_dedents;
        return make_token(lexer, TOKEN_DEDENT, lexer->position, lexer->line);
    }
    if (lexer->at_line_start) {
        size_t start = lexer->position;
        size_t indent = 0;
        while (!at_end(lexer) && (peek(lexer) == ' ' || peek(lexer) == '\t')) {
            indent += advance(lexer) == '\t' ? 4 : 1;
        }
        if (at_end(lexer)) {
            while (lexer->indent_count > 1) {
                --lexer->indent_count;
                return make_token(lexer, TOKEN_DEDENT, start, lexer->line);
            }
            return make_token(lexer, TOKEN_EOF, lexer->position, lexer->line);
        }
        if (peek(lexer) == '\n' || peek(lexer) == '#') {
            while (!at_end(lexer) && peek(lexer) != '\n') advance(lexer);
            if (!at_end(lexer)) {
                start = lexer->position;
                advance(lexer);
                ++lexer->line;
                lexer->at_line_start = 1;
                return make_token(lexer, TOKEN_NEWLINE, start, lexer->line - 1);
            }
            return make_token(lexer, TOKEN_EOF, lexer->position, lexer->line);
        }
        lexer->at_line_start = 0;
        if (indent > lexer->indent_levels[lexer->indent_count - 1]) {
            if (lexer->indent_count == sizeof(lexer->indent_levels)
                                         / sizeof(lexer->indent_levels[0])) {
                return error_token(lexer, start, lexer->line,
                                   "Слишком много уровней отступа");
            }
            lexer->indent_levels[lexer->indent_count++] = indent;
            return make_token(lexer, TOKEN_INDENT, start, lexer->line);
        }
        if (indent < lexer->indent_levels[lexer->indent_count - 1]) {
            while (lexer->indent_count > 1
                   && indent < lexer->indent_levels[lexer->indent_count - 1]) {
                --lexer->indent_count;
                ++lexer->pending_dedents;
            }
            if (indent != lexer->indent_levels[lexer->indent_count - 1]) {
                return error_token(lexer, start, lexer->line,
                                   "Отступ не совпадает с предыдущим уровнем");
            }
            --lexer->pending_dedents;
            return make_token(lexer, TOKEN_DEDENT, start, lexer->line);
        }
    }

    for (;;) {
        if (at_end(lexer)) {
            while (lexer->indent_count > 1) {
                --lexer->indent_count;
                return make_token(lexer, TOKEN_DEDENT, lexer->position, lexer->line);
            }
            return make_token(lexer, TOKEN_EOF, lexer->position, lexer->line);
        }
        c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') advance(lexer);
        else if (c == '#') {
            while (!at_end(lexer) && peek(lexer) != '\n') advance(lexer);
        } else break;
    }

    start = lexer->position;
    line = lexer->line;
    c = advance(lexer);

    if (c == '\n') {
        ++lexer->line;
        lexer->at_line_start = 1;
        return make_token(lexer, TOKEN_NEWLINE, start, line);
    }
    if ((c >= '0' && c <= '9')) {
        return scan_number(lexer, start, line);
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
        || c >= 0x80) {
        while (!at_end(lexer)) {
            c = peek(lexer);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '_' || c >= 0x80) {
                advance(lexer);
            } else {
                break;
            }
        }
        token = make_token(lexer, TOKEN_IDENTIFIER, start, line);
        token.type = identifier_type(token.start, token.length);
        return token;
    }
    if (c == '"' || c == '\'') {
        unsigned char quote = c;
        while (!at_end(lexer) && peek(lexer) != quote) {
            if (peek(lexer) == '\n') {
                ++lexer->line;
            }
            if (peek(lexer) == '\\' && lexer->position + 1 < lexer->length) {
                advance(lexer);
            }
            advance(lexer);
        }
        if (at_end(lexer)) {
            return error_token(lexer, start, line, "Незакрытая строка");
        }
        advance(lexer);
        return make_token(lexer, TOKEN_STRING, start, line);
    }

#define SINGLE(ch, kind) case ch: return make_token(lexer, kind, start, line)
#define DOUBLE(ch, next, yes, no) \
    case ch: \
        if (peek(lexer) == next) { \
            advance(lexer); \
            return make_token(lexer, yes, start, line); \
        } \
        return make_token(lexer, no, start, line)

    switch (c) {
        SINGLE('(', TOKEN_LEFT_PAREN);
        SINGLE(')', TOKEN_RIGHT_PAREN);
        SINGLE('[', TOKEN_LEFT_BRACKET);
        SINGLE(']', TOKEN_RIGHT_BRACKET);
        SINGLE('{', TOKEN_LEFT_BRACE);
        SINGLE('}', TOKEN_RIGHT_BRACE);
        SINGLE(',', TOKEN_COMMA);
        SINGLE(':', TOKEN_COLON);
        SINGLE('+', TOKEN_PLUS);
        SINGLE('-', TOKEN_MINUS);
        DOUBLE('*', '*', TOKEN_POWER, TOKEN_STAR);
        SINGLE('/', TOKEN_SLASH);
        SINGLE('%', TOKEN_PERCENT);
        DOUBLE('=', '=', TOKEN_EQUAL_EQUAL, TOKEN_EQUAL);
        case '!':
            if (peek(lexer) == '=') {
                advance(lexer);
                return make_token(lexer, TOKEN_BANG_EQUAL, start, line);
            }
            return error_token(lexer, start, line,
                               "После «!» ожидался знак «=»");
        DOUBLE('<', '=', TOKEN_LESS_EQUAL, TOKEN_LESS);
        DOUBLE('>', '=', TOKEN_GREATER_EQUAL, TOKEN_GREATER);
    default:
        return error_token(lexer, start, line, "Неизвестный символ");
    }

#undef SINGLE
#undef DOUBLE
}

const char *token_type_name(TokenType type)
{
    static const char *const names[] = {
        "ошибка", "конец ввода", "новая строка", "отступ", "конец блока", "идентификатор", "число",
        "строка", "(", ")", "[", "]", "{", "}", ",", ":", "+", "-",
        "*", "**", "/", "%", "=", "==", "!=", "<", "<=", ">", ">=",
        "пусть", "сказать", "спросить", "если", "иначе", "пока", "повторить",
        "раз", "как", "функция", "вернуть", "и", "или", "не", "да", "нет",
        "для", "каждого", "в"
    };
    if ((size_t)type >= sizeof(names) / sizeof(names[0])) {
        return "неизвестный токен";
    }
    return names[type];
}
