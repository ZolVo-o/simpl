#ifndef SIMPL_SML_LEXER_H
#define SIMPL_SML_LEXER_H

#include <stddef.h>

typedef enum {
    SML_TOKEN_ERROR,
    SML_TOKEN_EOF,
    SML_TOKEN_NEWLINE,
    SML_TOKEN_INDENT,
    SML_TOKEN_DEDENT,
    SML_TOKEN_TAG,
    SML_TOKEN_END,
    SML_TOKEN_ATTRIBUTE,
    SML_TOKEN_IDENTIFIER,
    SML_TOKEN_STRING,
    SML_TOKEN_NUMBER,
    SML_TOKEN_EQUAL
} SmlTokenType;

typedef struct {
    SmlTokenType type;
    const char *start;
    size_t length;
    size_t line;
    size_t column;
    double number;
    const char *error;
} SmlToken;

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
    size_t indent_levels[128];
    size_t indent_count;
    size_t pending_dedents;
    int at_line_start;
    int line_has_tokens;
    int eof_newline_emitted;
    int failed;
} SmlLexer;

void sml_lexer_init(SmlLexer *lexer, const char *source, size_t length);
SmlToken sml_lexer_next(SmlLexer *lexer);
const char *sml_token_type_name(SmlTokenType type);

#endif
