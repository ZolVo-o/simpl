#ifndef SIMPL_OPCODES_H
#define SIMPL_OPCODES_H

typedef enum {
    OP_CONST,
    OP_LOAD,
    OP_STORE,
    OP_SAY,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_AND,
    OP_OR,
    OP_NEG,
    OP_NOT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_CALL,
    OP_RETURN,
    OP_MAKE_LIST,
    OP_MAKE_DICT,
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_GET_ITER,
    OP_ITER_NEXT,
    OP_POP,
    OP_HALT,
    OP_ASK,
    OP_CALL_BUILTIN,
    OP_TRY_BEGIN,
    OP_TRY_END
} OpCode;

typedef enum {
    BUILTIN_LENGTH,
    BUILTIN_TYPE,
    BUILTIN_UPPER,
    BUILTIN_LOWER,
    BUILTIN_SPLIT,
    BUILTIN_JOIN
} BuiltinId;

#endif
