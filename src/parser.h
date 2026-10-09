#ifndef SIMPL_PARSER_H
#define SIMPL_PARSER_H

#include "ast.h"

typedef struct {
    Lexer lexer;
    Token current;
    const char *error;
    size_t error_line;
} Parser;

void parser_init(Parser *parser, const char *source, size_t length);
Node *parser_parse(Parser *parser);

#endif
