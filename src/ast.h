#ifndef SIMPL_AST_H
#define SIMPL_AST_H

#include "lexer.h"

typedef enum {
    NODE_PROGRAM,
    NODE_BLOCK,
    NODE_LET,
    NODE_SAY,
    NODE_ASSIGN,
    NODE_IF,
    NODE_WHILE,
    NODE_REPEAT,
    NODE_FOR_EACH,
    NODE_FUNC_DECL,
    NODE_RETURN,
    NODE_CALL,
    NODE_BINOP,
    NODE_UNOP,
    NODE_NUM,
    NODE_STR,
    NODE_BOOL,
    NODE_VAR,
    NODE_ASK,
    NODE_LIST,
    NODE_DICT,
    NODE_INDEX_GET,
    NODE_INDEX_SET,
    NODE_TRY_CATCH
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType type;
    size_t line;
    union {
        struct { Node **items; size_t count; } program;
        struct { char *name; Node *value; } declaration;
        struct { Node *expr; } say;
        struct { char *name; Node *value; } assign;
        struct { char *name; char **params; size_t param_count; Node *body; } function;
        struct { Node *value; } return_stmt;
        struct { char *name; Node **args; size_t arg_count; } call;
        struct { Node *condition; Node *then_branch; Node *else_branch; } if_stmt;
        struct { Node *condition; Node *body; } while_stmt;
        struct { Node *count; char *variable; Node *body; } repeat_stmt;
        struct { char *variable; Node *iterable; Node *body; } for_each;
        struct { Node **items; size_t count; } list;
        struct { Node **keys; Node **values; size_t count; } dict;
        struct { Node *target; Node *index; } index_get;
        struct { Node *target; Node *index; Node *value; } index_set;
        struct { Node *body; char *error_name; Node *handler; } try_catch;
        struct { TokenType op; Node *left; Node *right; } binop;
        struct { TokenType op; Node *value; } unop;
        struct { double value; } number;
        struct { char *value; } string;
        struct { int value; } boolean;
        struct { char *name; } variable;
    } as;
};

void ast_free(Node *node);
void ast_print(const Node *node);

#endif
