#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *text, size_t length)
{
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static Node *new_node(NodeType type, size_t line)
{
    Node *node = calloc(1, sizeof(*node));
    if (node != NULL) {
        node->type = type;
        node->line = line;
    }
    return node;
}

void ast_free(Node *node)
{
    size_t i;
    if (node == NULL) {
        return;
    }
    switch (node->type) {
    case NODE_PROGRAM:
    case NODE_BLOCK:
        for (i = 0; i < node->as.program.count; ++i) {
            ast_free(node->as.program.items[i]);
        }
        free(node->as.program.items);
        break;
    case NODE_LET:
        free(node->as.declaration.name);
        ast_free(node->as.declaration.value);
        break;
    case NODE_SAY:
        ast_free(node->as.say.expr);
        break;
    case NODE_ASSIGN:
        free(node->as.assign.name);
        ast_free(node->as.assign.value);
        break;
    case NODE_IF:
        ast_free(node->as.if_stmt.condition);
        ast_free(node->as.if_stmt.then_branch);
        ast_free(node->as.if_stmt.else_branch);
        break;
    case NODE_WHILE:
        ast_free(node->as.while_stmt.condition);
        ast_free(node->as.while_stmt.body);
        break;
    case NODE_REPEAT:
        ast_free(node->as.repeat_stmt.count);
        free(node->as.repeat_stmt.variable);
        ast_free(node->as.repeat_stmt.body);
        break;
    case NODE_FOR_EACH:
        free(node->as.for_each.variable);
        ast_free(node->as.for_each.iterable);
        ast_free(node->as.for_each.body);
        break;
    case NODE_LIST:
        for (i = 0; i < node->as.list.count; ++i) ast_free(node->as.list.items[i]);
        free(node->as.list.items);
        break;
    case NODE_DICT:
        for (i = 0; i < node->as.dict.count; ++i) {
            ast_free(node->as.dict.keys[i]);
            ast_free(node->as.dict.values[i]);
        }
        free(node->as.dict.keys);
        free(node->as.dict.values);
        break;
    case NODE_INDEX_GET:
        ast_free(node->as.index_get.target);
        ast_free(node->as.index_get.index);
        break;
    case NODE_INDEX_SET:
        ast_free(node->as.index_set.target);
        ast_free(node->as.index_set.index);
        ast_free(node->as.index_set.value);
        break;
    case NODE_TRY_CATCH:
        ast_free(node->as.try_catch.body);
        free(node->as.try_catch.error_name);
        ast_free(node->as.try_catch.handler);
        break;
    case NODE_FUNC_DECL:
        free(node->as.function.name);
        for (i = 0; i < node->as.function.param_count; ++i) {
            free(node->as.function.params[i]);
        }
        free(node->as.function.params);
        ast_free(node->as.function.body);
        break;
    case NODE_RETURN:
        ast_free(node->as.return_stmt.value);
        break;
    case NODE_ASK:
        break;
    case NODE_CALL:
        free(node->as.call.name);
        for (i = 0; i < node->as.call.arg_count; ++i) ast_free(node->as.call.args[i]);
        free(node->as.call.args);
        break;
    case NODE_BINOP:
        ast_free(node->as.binop.left);
        ast_free(node->as.binop.right);
        break;
    case NODE_UNOP:
        ast_free(node->as.unop.value);
        break;
    case NODE_STR:
        free(node->as.string.value);
        break;
    case NODE_VAR:
        free(node->as.variable.name);
        break;
    case NODE_NUM:
    case NODE_BOOL:
        break;
    }
    free(node);
}

static void advance_token(Parser *parser)
{
    parser->current = lexer_next(&parser->lexer);
    if (parser->current.type == TOKEN_ERROR && parser->error == NULL) {
        parser->error = parser->current.error;
        parser->error_line = parser->current.line;
    }
}

static int accept(Parser *parser, TokenType type)
{
    if (parser->current.type != type) {
        return 0;
    }
    advance_token(parser);
    return 1;
}

static void fail(Parser *parser, const char *message)
{
    if (parser->error == NULL) {
        parser->error = message;
        parser->error_line = parser->current.line;
    }
}

static int expect(Parser *parser, TokenType type, const char *message)
{
    if (accept(parser, type)) {
        return 1;
    }
    fail(parser, message);
    return 0;
}

static int precedence(TokenType type)
{
    switch (type) {
    case TOKEN_OR: return 1;
    case TOKEN_AND: return 2;
    case TOKEN_EQUAL_EQUAL:
    case TOKEN_BANG_EQUAL: return 3;
    case TOKEN_LESS:
    case TOKEN_LESS_EQUAL:
    case TOKEN_GREATER:
    case TOKEN_GREATER_EQUAL: return 4;
    case TOKEN_PLUS:
    case TOKEN_MINUS: return 5;
    case TOKEN_STAR:
    case TOKEN_SLASH:
    case TOKEN_PERCENT: return 6;
    case TOKEN_POWER: return 8;
    default: return 0;
    }
}

static Node *parse_expression(Parser *parser, int min_precedence);

static Node *parse_primary(Parser *parser)
{
    Token token = parser->current;
    Node *node;

    switch (token.type) {
    case TOKEN_NUMBER:
        advance_token(parser);
        node = new_node(NODE_NUM, token.line);
        if (node != NULL) node->as.number.value = token.number;
        return node;
    case TOKEN_STRING:
        advance_token(parser);
        node = new_node(NODE_STR, token.line);
        if (node != NULL) {
            node->as.string.value = copy_text(token.start + 1, token.length - 2);
            if (node->as.string.value == NULL) {
                ast_free(node);
                return NULL;
            }
        }
        return node;
    case TOKEN_TRUE:
    case TOKEN_FALSE:
        advance_token(parser);
        node = new_node(NODE_BOOL, token.line);
        if (node != NULL) node->as.boolean.value = token.type == TOKEN_TRUE;
        return node;
    case TOKEN_ASK:
        advance_token(parser);
        return new_node(NODE_ASK, token.line);
    case TOKEN_LEFT_BRACKET: {
        size_t count = 0;
        Node **items = NULL;
        advance_token(parser);
        if (parser->current.type != TOKEN_RIGHT_BRACKET) {
            do {
                Node *item = parse_expression(parser, 1);
                Node **grown;
                if (item == NULL) goto list_fail;
                grown = realloc(items, (count + 1) * sizeof(*grown));
                if (grown == NULL) { ast_free(item); goto list_fail; }
                items = grown;
                items[count++] = item;
            } while (accept(parser, TOKEN_COMMA));
        }
        if (!expect(parser, TOKEN_RIGHT_BRACKET, "Ожидалась «]»")) goto list_fail;
        node = new_node(NODE_LIST, token.line);
        if (node == NULL) goto list_fail;
        node->as.list.items = items;
        node->as.list.count = count;
        return node;
list_fail:
        while (count > 0) ast_free(items[--count]);
        free(items);
        return NULL;
    }
    case TOKEN_LEFT_BRACE: {
        size_t count = 0;
        Node **keys = NULL;
        Node **values = NULL;
        advance_token(parser);
        if (parser->current.type != TOKEN_RIGHT_BRACE) {
            do {
                Node *key = parse_expression(parser, 1);
                Node *value;
                Node **grown_keys;
                Node **grown_values;
                if (key == NULL || !expect(parser, TOKEN_COLON, "После ключа ожидалось «:»")) {
                    ast_free(key);
                    goto dict_fail;
                }
                value = parse_expression(parser, 1);
                if (value == NULL) { ast_free(key); goto dict_fail; }
                grown_keys = realloc(keys, (count + 1) * sizeof(*grown_keys));
                if (grown_keys == NULL) { ast_free(key); ast_free(value); goto dict_fail; }
                keys = grown_keys;
                grown_values = realloc(values, (count + 1) * sizeof(*grown_values));
                if (grown_values == NULL) { ast_free(key); ast_free(value); goto dict_fail; }
                values = grown_values;
                keys[count] = key;
                values[count++] = value;
            } while (accept(parser, TOKEN_COMMA));
        }
        if (!expect(parser, TOKEN_RIGHT_BRACE, "Ожидалась «}»")) goto dict_fail;
        node = new_node(NODE_DICT, token.line);
        if (node == NULL) goto dict_fail;
        node->as.dict.keys = keys;
        node->as.dict.values = values;
        node->as.dict.count = count;
        return node;
dict_fail:
        while (count > 0) { --count; ast_free(keys[count]); ast_free(values[count]); }
        free(keys);
        free(values);
        return NULL;
    }
    case TOKEN_IDENTIFIER:
        advance_token(parser);
        if (accept(parser, TOKEN_LEFT_PAREN)) {
            size_t count = 0;
            Node **args = NULL;
            node = new_node(NODE_CALL, token.line);
            if (node == NULL) return NULL;
            node->as.call.name = copy_text(token.start, token.length);
            if (parser->current.type != TOKEN_RIGHT_PAREN) {
                do {
                    Node *arg = parse_expression(parser, 1);
                    Node **grown;
                    if (arg == NULL) {
                        node->as.call.args = args;
                        node->as.call.arg_count = count;
                        ast_free(node);
                        return NULL;
                    }
                    grown = realloc(args, (count + 1) * sizeof(*grown));
                    if (grown == NULL) {
                        ast_free(arg);
                        node->as.call.args = args;
                        node->as.call.arg_count = count;
                        ast_free(node);
                        return NULL;
                    }
                    args = grown;
                    args[count++] = arg;
                    node->as.call.args = args;
                    node->as.call.arg_count = count;
                } while (accept(parser, TOKEN_COMMA));
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN, "Ожидалась закрывающая скобка «)»")
                || node->as.call.name == NULL) {
                ast_free(node);
                return NULL;
            }
            node->as.call.args = args;
            node->as.call.arg_count = count;
            return node;
        }
        node = new_node(NODE_VAR, token.line);
        if (node != NULL) {
            node->as.variable.name = copy_text(token.start, token.length);
            if (node->as.variable.name == NULL) {
                ast_free(node);
                return NULL;
            }
        }
        return node;
    case TOKEN_LEFT_PAREN:
        advance_token(parser);
        node = parse_expression(parser, 1);
        if (node == NULL) return NULL;
        if (!expect(parser, TOKEN_RIGHT_PAREN, "Ожидалась закрывающая скобка «)»")) {
            ast_free(node);
            return NULL;
        }
        return node;
    case TOKEN_MINUS:
    case TOKEN_NOT: {
        advance_token(parser);
        node = new_node(NODE_UNOP, token.line);
        if (node == NULL) return NULL;
        node->as.unop.op = token.type;
        node->as.unop.value = parse_expression(parser, 7);
        if (node->as.unop.value == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }
    default:
        fail(parser, "Ожидалось выражение");
        return NULL;
    }
}

static Node *parse_expression(Parser *parser, int min_precedence)
{
    Node *left = parse_primary(parser);
    if (left == NULL) return NULL;

    while (parser->current.type == TOKEN_LEFT_BRACKET) {
        Node *index;
        Node *get;
        advance_token(parser);
        index = parse_expression(parser, 1);
        if (index == NULL || !expect(parser, TOKEN_RIGHT_BRACKET, "Ожидалась «]»")) {
            ast_free(left);
            ast_free(index);
            return NULL;
        }
        get = new_node(NODE_INDEX_GET, left->line);
        if (get == NULL) { ast_free(left); ast_free(index); return NULL; }
        get->as.index_get.target = left;
        get->as.index_get.index = index;
        left = get;
    }

    while (precedence(parser->current.type) >= min_precedence
           && precedence(parser->current.type) > 0) {
        Token op = parser->current;
        int op_precedence = precedence(op.type);
        Node *right;
        Node *combined;

        advance_token(parser);
        right = parse_expression(parser,
                                 op_precedence + (op.type == TOKEN_POWER ? 0 : 1));
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }
        combined = new_node(NODE_BINOP, op.line);
        if (combined == NULL) {
            ast_free(left);
            ast_free(right);
            return NULL;
        }
        combined->as.binop.op = op.type;
        combined->as.binop.left = left;
        combined->as.binop.right = right;
        left = combined;
    }
    return left;
}

static Node *parse_block(Parser *parser);

static int has_block_body(const Node *node)
{
    return node->type == NODE_IF || node->type == NODE_WHILE
        || node->type == NODE_REPEAT || node->type == NODE_FOR_EACH
        || node->type == NODE_FUNC_DECL || node->type == NODE_TRY_CATCH;
}

static Node *parse_statement(Parser *parser)
{
    Token token = parser->current;
    Node *node;

    if (accept(parser, TOKEN_LET)) {
        Token name = parser->current;
        if (!expect(parser, TOKEN_IDENTIFIER, "После «пусть» ожидалось имя переменной")) {
            return NULL;
        }
        if (!expect(parser, TOKEN_EQUAL, "После имени переменной ожидался знак «=»")) {
            return NULL;
        }
        node = new_node(NODE_LET, token.line);
        if (node == NULL) return NULL;
        node->as.declaration.name = copy_text(name.start, name.length);
        node->as.declaration.value = parse_expression(parser, 1);
        if (node->as.declaration.name == NULL || node->as.declaration.value == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_SAY)) {
        node = new_node(NODE_SAY, token.line);
        if (node == NULL) return NULL;
        node->as.say.expr = parse_expression(parser, 1);
        if (node->as.say.expr == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_IF)) {
        node = new_node(NODE_IF, token.line);
        if (node == NULL) return NULL;
        node->as.if_stmt.condition = parse_expression(parser, 1);
        if (node->as.if_stmt.condition == NULL
            || !expect(parser, TOKEN_NEWLINE, "После условия ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            if (parser->error == NULL) fail(parser, "После «если» ожидался блок с отступом");
            ast_free(node);
            return NULL;
        }
        node->as.if_stmt.then_branch = parse_block(parser);
        if (node->as.if_stmt.then_branch == NULL) {
            ast_free(node);
            return NULL;
        }
        if (parser->current.type == TOKEN_NEWLINE) advance_token(parser);
        if (accept(parser, TOKEN_ELSE)) {
            if (!expect(parser, TOKEN_NEWLINE, "После «иначе» ожидался конец строки")
                || parser->current.type != TOKEN_INDENT) {
                if (parser->error == NULL) fail(parser, "После «иначе» ожидался блок с отступом");
                ast_free(node);
                return NULL;
            }
            node->as.if_stmt.else_branch = parse_block(parser);
            if (node->as.if_stmt.else_branch == NULL) {
                ast_free(node);
                return NULL;
            }
        }
        return node;
    }

    if (accept(parser, TOKEN_WHILE)) {
        node = new_node(NODE_WHILE, token.line);
        if (node == NULL) return NULL;
        node->as.while_stmt.condition = parse_expression(parser, 1);
        if (node->as.while_stmt.condition == NULL
            || !expect(parser, TOKEN_NEWLINE, "После условия ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            if (parser->error == NULL) fail(parser, "После «пока» ожидался блок с отступом");
            ast_free(node);
            return NULL;
        }
        node->as.while_stmt.body = parse_block(parser);
        if (node->as.while_stmt.body == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_REPEAT)) {
        node = new_node(NODE_REPEAT, token.line);
        if (node == NULL) return NULL;
        node->as.repeat_stmt.count = parse_expression(parser, 1);
        if (node->as.repeat_stmt.count == NULL
            || !expect(parser, TOKEN_TIMES, "После количества ожидалось слово «раз»")
            || !expect(parser, TOKEN_AS, "После «раз» ожидалось слово «как»")) {
            ast_free(node);
            return NULL;
        }
        {
            Token variable = parser->current;
            if (!expect(parser, TOKEN_IDENTIFIER, "После «как» ожидалось имя счётчика")
                || !expect(parser, TOKEN_NEWLINE, "После цикла ожидался конец строки")
                || parser->current.type != TOKEN_INDENT) {
                if (parser->error == NULL) fail(parser, "После цикла ожидался блок с отступом");
                ast_free(node);
                return NULL;
            }
            node->as.repeat_stmt.variable = copy_text(variable.start, variable.length);
        }
        node->as.repeat_stmt.body = parse_block(parser);
        if (node->as.repeat_stmt.variable == NULL || node->as.repeat_stmt.body == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_FOR)) {
        Token variable;
        node = new_node(NODE_FOR_EACH, token.line);
        if (node == NULL) return NULL;
        if (!expect(parser, TOKEN_EACH, "После «для» ожидалось «каждого»")) {
            ast_free(node);
            return NULL;
        }
        variable = parser->current;
        if (!expect(parser, TOKEN_IDENTIFIER, "После «каждого» ожидалось имя переменной")
            || !expect(parser, TOKEN_IN, "После переменной ожидалось «в»")) {
            ast_free(node);
            return NULL;
        }
        node->as.for_each.variable = copy_text(variable.start, variable.length);
        node->as.for_each.iterable = parse_expression(parser, 1);
        if (node->as.for_each.variable == NULL || node->as.for_each.iterable == NULL
            || !expect(parser, TOKEN_NEWLINE, "После цикла ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            if (parser->error == NULL) fail(parser, "После цикла ожидался блок с отступом");
            ast_free(node);
            return NULL;
        }
        node->as.for_each.body = parse_block(parser);
        if (node->as.for_each.body == NULL) { ast_free(node); return NULL; }
        return node;
    }

    if (accept(parser, TOKEN_TRY)) {
        Token error_name;
        node = new_node(NODE_TRY_CATCH, token.line);
        if (node == NULL) return NULL;
        if (!expect(parser, TOKEN_NEWLINE, "После «попробовать» ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            if (parser->error == NULL) fail(parser, "После «попробовать» ожидался блок");
            ast_free(node);
            return NULL;
        }
        node->as.try_catch.body = parse_block(parser);
        if (node->as.try_catch.body == NULL
            || !expect(parser, TOKEN_CATCH, "После блока ожидалось «поймать»")) {
            ast_free(node);
            return NULL;
        }
        error_name = parser->current;
        if (!expect(parser, TOKEN_IDENTIFIER, "После «поймать» ожидалось имя ошибки")
            || !expect(parser, TOKEN_NEWLINE, "После имени ошибки ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            if (parser->error == NULL) fail(parser, "После «поймать» ожидался блок");
            ast_free(node);
            return NULL;
        }
        node->as.try_catch.error_name = copy_text(error_name.start, error_name.length);
        node->as.try_catch.handler = parse_block(parser);
        if (node->as.try_catch.error_name == NULL || node->as.try_catch.handler == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_FUNCTION)) {
        Token name = parser->current;
        size_t count = 0;
        char **params = NULL;
        node = new_node(NODE_FUNC_DECL, token.line);
        if (node == NULL) return NULL;
        if (!expect(parser, TOKEN_IDENTIFIER, "После «функция» ожидалось имя" )
            || !expect(parser, TOKEN_LEFT_PAREN, "После имени функции ожидалась «(»")) {
            ast_free(node);
            return NULL;
        }
        node->as.function.name = copy_text(name.start, name.length);
        if (parser->current.type != TOKEN_RIGHT_PAREN) {
            do {
                Token parameter = parser->current;
                char **grown;
                if (!expect(parser, TOKEN_IDENTIFIER, "Ожидалось имя параметра")) {
                    free(params);
                    ast_free(node);
                    return NULL;
                }
                grown = realloc(params, (count + 1) * sizeof(*grown));
                if (grown == NULL) {
                    free(params);
                    ast_free(node);
                    return NULL;
                }
                params = grown;
                params[count] = copy_text(parameter.start, parameter.length);
                if (params[count] == NULL) {
                    while (count > 0) free(params[--count]);
                    free(params);
                    ast_free(node);
                    return NULL;
                }
                ++count;
            } while (accept(parser, TOKEN_COMMA));
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "Ожидалась закрывающая скобка «)»")
            || !expect(parser, TOKEN_NEWLINE, "После объявления функции ожидался конец строки")
            || parser->current.type != TOKEN_INDENT) {
            size_t i;
            if (parser->error == NULL) fail(parser, "После функции ожидался блок с отступом");
            for (i = 0; i < count; ++i) free(params[i]);
            free(params);
            ast_free(node);
            return NULL;
        }
        node->as.function.params = params;
        node->as.function.param_count = count;
        node->as.function.body = parse_block(parser);
        if (node->as.function.name == NULL || node->as.function.body == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (accept(parser, TOKEN_RETURN)) {
        node = new_node(NODE_RETURN, token.line);
        if (node == NULL) return NULL;
        node->as.return_stmt.value = parse_expression(parser, 1);
        if (node->as.return_stmt.value == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    if (token.type == TOKEN_IDENTIFIER) {
        advance_token(parser);
        if (accept(parser, TOKEN_LEFT_BRACKET)) {
            Node *target = new_node(NODE_VAR, token.line);
            Node *index;
            Node *value;
            if (target == NULL) return NULL;
            target->as.variable.name = copy_text(token.start, token.length);
            index = parse_expression(parser, 1);
            if (target->as.variable.name == NULL || index == NULL
                || !expect(parser, TOKEN_RIGHT_BRACKET, "Ожидалась «]»")
                || !expect(parser, TOKEN_EQUAL, "После индекса ожидался знак «=»")) {
                ast_free(target);
                ast_free(index);
                return NULL;
            }
            value = parse_expression(parser, 1);
            if (value == NULL) {
                ast_free(target);
                ast_free(index);
                return NULL;
            }
            node = new_node(NODE_INDEX_SET, token.line);
            if (node == NULL) {
                ast_free(target); ast_free(index); ast_free(value);
                return NULL;
            }
            node->as.index_set.target = target;
            node->as.index_set.index = index;
            node->as.index_set.value = value;
            return node;
        }
        if (!expect(parser, TOKEN_EQUAL, "После имени переменной ожидался знак «=»")) {
            return NULL;
        }
        node = new_node(NODE_ASSIGN, token.line);
        if (node == NULL) return NULL;
        node->as.assign.name = copy_text(token.start, token.length);
        node->as.assign.value = parse_expression(parser, 1);
        if (node->as.assign.name == NULL || node->as.assign.value == NULL) {
            ast_free(node);
            return NULL;
        }
        return node;
    }

    fail(parser, "Ожидалась инструкция «пусть» или «сказать»");
    return NULL;
}

static Node *parse_block(Parser *parser)
{
    Node *block = new_node(NODE_BLOCK, parser->current.line);
    if (block == NULL) return NULL;
    advance_token(parser);

    while (parser->current.type != TOKEN_DEDENT
           && parser->current.type != TOKEN_EOF
           && parser->error == NULL) {
        Node *child;
        Node **items;
        if (accept(parser, TOKEN_NEWLINE)) continue;
        child = parser->current.type == TOKEN_INDENT
            ? parse_block(parser) : parse_statement(parser);
        if (child == NULL) break;
        items = realloc(block->as.program.items,
                        (block->as.program.count + 1) * sizeof(*items));
        if (items == NULL) {
            ast_free(child);
            fail(parser, "Недостаточно памяти для AST");
            break;
        }
        block->as.program.items = items;
        items[block->as.program.count++] = child;
        if (parser->current.type == TOKEN_NEWLINE) advance_token(parser);
        else if (parser->current.type != TOKEN_DEDENT && !has_block_body(child)) {
            fail(parser, "Ожидался конец строки после инструкции");
        }
    }

    if (parser->error != NULL
        || !expect(parser, TOKEN_DEDENT, "Ожидался конец блока")) {
        ast_free(block);
        return NULL;
    }
    return block;
}

void parser_init(Parser *parser, const char *source, size_t length)
{
    lexer_init(&parser->lexer, source, length);
    parser->error = NULL;
    parser->error_line = 0;
    parser->current.type = TOKEN_ERROR;
    advance_token(parser);
}

Node *parser_parse(Parser *parser)
{
    Node *program = new_node(NODE_PROGRAM, 1);
    if (program == NULL) return NULL;

    while (parser->current.type != TOKEN_EOF && parser->error == NULL) {
        Node *statement;
        if (accept(parser, TOKEN_NEWLINE)) continue;

        if (parser->current.type == TOKEN_INDENT) {
            statement = parse_block(parser);
            if (statement == NULL) {
                ast_free(program);
                return NULL;
            }
        } else {
            statement = parse_statement(parser);
            if (statement == NULL) {
                ast_free(program);
                return NULL;
            }
        }
        {
            Node **items = realloc(program->as.program.items,
                (program->as.program.count + 1) * sizeof(*items));
            if (items == NULL) {
                ast_free(statement);
                ast_free(program);
                return NULL;
            }
            program->as.program.items = items;
            items[program->as.program.count++] = statement;
        }

        if (parser->current.type == TOKEN_NEWLINE) {
            advance_token(parser);
        } else if (parser->current.type != TOKEN_EOF && !has_block_body(statement)) {
            fail(parser, "Ожидался конец строки после инструкции");
        }
    }

    if (parser->error != NULL) {
        ast_free(program);
        return NULL;
    }
    return program;
}

static void print_expression(const Node *node)
{
    if (node == NULL) return;
    switch (node->type) {
    case NODE_NUM:
        printf("%.15g", node->as.number.value);
        break;
    case NODE_STR:
        printf("\"%s\"", node->as.string.value);
        break;
    case NODE_BOOL:
        fputs(node->as.boolean.value ? "да" : "нет", stdout);
        break;
    case NODE_VAR:
        fputs(node->as.variable.name, stdout);
        break;
    case NODE_UNOP:
        printf("(%s ", token_type_name(node->as.unop.op));
        print_expression(node->as.unop.value);
        putchar(')');
        break;
    case NODE_BINOP:
        putchar('(');
        print_expression(node->as.binop.left);
        printf(" %s ", token_type_name(node->as.binop.op));
        print_expression(node->as.binop.right);
        putchar(')');
        break;
    case NODE_PROGRAM:
    case NODE_BLOCK:
    case NODE_LET:
    case NODE_SAY:
    case NODE_ASSIGN:
    case NODE_IF:
    case NODE_WHILE:
    case NODE_REPEAT:
    case NODE_TRY_CATCH:
        break;
    case NODE_FUNC_DECL:
        printf("(функция %s)", node->as.function.name);
        break;
    case NODE_RETURN:
        fputs("(вернуть ", stdout);
        print_expression(node->as.return_stmt.value);
        putchar(')');
        break;
    case NODE_CALL:
        printf("(вызов %s)", node->as.call.name);
        break;
    case NODE_ASK:
        fputs("(спросить)", stdout);
        break;
    case NODE_LIST:
    case NODE_DICT:
    case NODE_INDEX_GET:
    case NODE_INDEX_SET:
    case NODE_FOR_EACH:
        break;
    }
}

void ast_print(const Node *node)
{
    size_t i;
    if (node == NULL) return;
    if (node->type == NODE_PROGRAM || node->type == NODE_BLOCK) {
        fputs("(программа", stdout);
        for (i = 0; i < node->as.program.count; ++i) {
            putchar(' ');
            ast_print(node->as.program.items[i]);
        }
        putchar(')');
    } else if (node->type == NODE_LET) {
        printf("(пусть %s ", node->as.declaration.name);
        print_expression(node->as.declaration.value);
        putchar(')');
    } else if (node->type == NODE_SAY) {
        fputs("(сказать ", stdout);
        print_expression(node->as.say.expr);
        putchar(')');
    } else if (node->type == NODE_ASSIGN) {
        printf("(присвоить %s ", node->as.assign.name);
        print_expression(node->as.assign.value);
        putchar(')');
    } else if (node->type == NODE_IF) {
        fputs("(если ", stdout);
        print_expression(node->as.if_stmt.condition);
        putchar(' ');
        ast_print(node->as.if_stmt.then_branch);
        if (node->as.if_stmt.else_branch != NULL) {
            putchar(' ');
            ast_print(node->as.if_stmt.else_branch);
        }
        putchar(')');
    } else if (node->type == NODE_WHILE) {
        fputs("(пока ", stdout);
        print_expression(node->as.while_stmt.condition);
        putchar(' ');
        ast_print(node->as.while_stmt.body);
        putchar(')');
    } else if (node->type == NODE_REPEAT) {
        fputs("(повторить ", stdout);
        print_expression(node->as.repeat_stmt.count);
        printf(" как %s ", node->as.repeat_stmt.variable);
        ast_print(node->as.repeat_stmt.body);
        putchar(')');
    } else if (node->type == NODE_TRY_CATCH) {
        fputs("(попробовать ", stdout);
        ast_print(node->as.try_catch.body);
        printf(" поймать %s ", node->as.try_catch.error_name);
        ast_print(node->as.try_catch.handler);
        putchar(')');
    }
}
