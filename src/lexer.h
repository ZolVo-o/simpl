#ifndef SIMPL_LEXER_H
#define SIMPL_LEXER_H

#include <stddef.h>

typedef enum {
    TOKEN_ERROR,
    TOKEN_EOF,
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_POWER,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LET,
    TOKEN_SAY,
    TOKEN_ASK,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_REPEAT,
    TOKEN_TIMES,
    TOKEN_AS,
    TOKEN_FUNCTION,
    TOKEN_RETURN,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_FOR,
    TOKEN_EACH,
    TOKEN_IN,
    TOKEN_TRY,
    TOKEN_CATCH
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    size_t length;
    size_t line;
    double number;
    const char *error;
} Token;

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t indent_levels[128];
    size_t indent_count;
    size_t pending_dedents;
    int at_line_start;
    int eof_processed;
} Lexer;

void lexer_init(Lexer *lexer, const char *source, size_t length);
Token lexer_next(Lexer *lexer);
const char *token_type_name(TokenType type);

#endif
