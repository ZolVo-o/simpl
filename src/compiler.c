#include "compiler.h"

#include "opcodes.h"

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compile_node(const Node *node, Chunk *chunk);

static int find_function(const Chunk *chunk, const char *name, uint32_t *index)
{
    size_t i;
    for (i = 0; i < chunk->function_count; ++i) {
        if (strcmp(chunk->names[chunk->functions[i].name_index], name) == 0) {
            *index = (uint32_t)i;
            return 1;
        }
    }
    return 0;
}

static int find_builtin(const char *name, uint32_t *index)
{
    static const char *const names[] = {
        "длина", "тип", "верх", "низ", "разделить", "соединить", "аргументы"
    };
    size_t i;
    if (strcmp(name, "arguments") == 0) {
        *index = BUILTIN_ARGUMENTS;
        return 1;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            *index = (uint32_t)i;
            return 1;
        }
    }
    return 0;
}

static int emit_index(Chunk *chunk, OpCode opcode, uint32_t index)
{
    return chunk_emit(chunk, (uint8_t)opcode) && chunk_emit_u32(chunk, index);
}

static size_t emit_jump(Chunk *chunk, OpCode opcode)
{
    size_t operand = chunk->code_count + 1;
    if (!chunk_emit(chunk, (uint8_t)opcode) || !chunk_emit_u32(chunk, 0)) return SIZE_MAX;
    return operand;
}

static int patch_jump(Chunk *chunk, size_t operand, size_t target)
{
    int64_t distance = (int64_t)target - (int64_t)(operand + 4);
    uint32_t encoded;
    if (distance < INT32_MIN || distance > INT32_MAX) return 0;
    encoded = (uint32_t)(int32_t)distance;
    chunk->code[operand] = (uint8_t)encoded;
    chunk->code[operand + 1] = (uint8_t)(encoded >> 8);
    chunk->code[operand + 2] = (uint8_t)(encoded >> 16);
    chunk->code[operand + 3] = (uint8_t)(encoded >> 24);
    return 1;
}

static int compile_binary(TokenType token, Chunk *chunk)
{
    OpCode opcode;
    switch (token) {
    case TOKEN_PLUS: opcode = OP_ADD; break;
    case TOKEN_MINUS: opcode = OP_SUB; break;
    case TOKEN_STAR: opcode = OP_MUL; break;
    case TOKEN_SLASH: opcode = OP_DIV; break;
    case TOKEN_PERCENT: opcode = OP_MOD; break;
    case TOKEN_POWER: opcode = OP_POW; break;
    case TOKEN_EQUAL_EQUAL: opcode = OP_EQ; break;
    case TOKEN_BANG_EQUAL: opcode = OP_NE; break;
    case TOKEN_LESS: opcode = OP_LT; break;
    case TOKEN_LESS_EQUAL: opcode = OP_LE; break;
    case TOKEN_GREATER: opcode = OP_GT; break;
    case TOKEN_GREATER_EQUAL: opcode = OP_GE; break;
    case TOKEN_AND: opcode = OP_AND; break;
    case TOKEN_OR: opcode = OP_OR; break;
    default: return 0;
    }
    return chunk_emit(chunk, (uint8_t)opcode);
}

static PyObject *number_value(double number)
{
    if (number >= (double)LONG_MIN && number < (double)LONG_MAX
        && number == (double)(long)number) return PyLong_FromLong((long)number);
    return PyFloat_FromDouble(number);
}

static int fold_constant(const Node *node, PyObject **result, unsigned int depth)
{
    PyObject *left = NULL, *right = NULL, *value = NULL;
    if (depth > 128) return 0;
    switch (node->type) {
    case NODE_NUM:
        value = number_value(node->as.number.value);
        break;
    case NODE_STR:
        value = PyUnicode_FromString(node->as.string.value);
        break;
    case NODE_BOOL:
        value = node->as.boolean.value ? Py_True : Py_False;
        Py_INCREF(value);
        break;
    case NODE_UNOP:
        if (!fold_constant(node->as.unop.value, &left, depth + 1)) return 0;
        if (node->as.unop.op == TOKEN_MINUS) value = PyNumber_Negative(left);
        else {
            int truth = PyObject_IsTrue(left);
            if (truth >= 0) value = PyBool_FromLong(!truth);
        }
        Py_DECREF(left);
        break;
    case NODE_BINOP:
        if (!fold_constant(node->as.binop.left, &left, depth + 1)
            || !fold_constant(node->as.binop.right, &right, depth + 1)) {
            Py_XDECREF(left); Py_XDECREF(right);
            return 0;
        }
        switch (node->as.binop.op) {
        case TOKEN_PLUS: value = PyNumber_Add(left, right); break;
        case TOKEN_MINUS: value = PyNumber_Subtract(left, right); break;
        case TOKEN_STAR: value = PyNumber_Multiply(left, right); break;
        case TOKEN_SLASH: value = PyNumber_TrueDivide(left, right); break;
        case TOKEN_PERCENT: value = PyNumber_Remainder(left, right); break;
        case TOKEN_POWER: value = PyNumber_Power(left, right, Py_None); break;
        case TOKEN_EQUAL_EQUAL: value = PyObject_RichCompare(left, right, Py_EQ); break;
        case TOKEN_BANG_EQUAL: value = PyObject_RichCompare(left, right, Py_NE); break;
        case TOKEN_LESS: value = PyObject_RichCompare(left, right, Py_LT); break;
        case TOKEN_LESS_EQUAL: value = PyObject_RichCompare(left, right, Py_LE); break;
        case TOKEN_GREATER: value = PyObject_RichCompare(left, right, Py_GT); break;
        case TOKEN_GREATER_EQUAL: value = PyObject_RichCompare(left, right, Py_GE); break;
        case TOKEN_AND:
        case TOKEN_OR: {
            int a = PyObject_IsTrue(left), b = PyObject_IsTrue(right);
            if (a >= 0 && b >= 0)
                value = PyBool_FromLong(node->as.binop.op == TOKEN_AND
                    ? a && b : a || b);
            break;
        }
        default: break;
        }
        Py_DECREF(left); Py_DECREF(right);
        break;
    default:
        return 0;
    }
    if (value == NULL) {
        PyErr_Clear();
        return 0;
    }
    *result = value;
    return 1;
}

static int add_constant_cached(Chunk *chunk, PyObject *value, uint32_t *index)
{
    size_t i;
    if (PyUnicode_Check(value)) {
        for (i = 0; i < chunk->constant_count; ++i) {
            if (PyUnicode_Check(chunk->constants[i])
                && PyUnicode_Compare(value, chunk->constants[i]) == 0) {
                *index = (uint32_t)i;
                return 1;
            }
        }
    }
    return chunk_add_constant(chunk, value, index);
}

static int compile_node(const Node *node, Chunk *chunk)
{
    uint32_t index;
    size_t i;

    chunk->current_line = node->line > UINT32_MAX ? UINT32_MAX : (uint32_t)node->line;
    if (node->type == NODE_BINOP || node->type == NODE_UNOP) {
        PyObject *folded;
        if (fold_constant(node, &folded, 0)) {
            int ok = add_constant_cached(chunk, folded, &index)
                && emit_index(chunk, OP_CONST, index);
            Py_DECREF(folded);
            return ok;
        }
    }
    switch (node->type) {
    case NODE_PROGRAM:
    case NODE_BLOCK:
        for (i = 0; i < node->as.program.count; ++i) {
            if (!compile_node(node->as.program.items[i], chunk)) return 0;
        }
        return 1;
    case NODE_FUNC_DECL:
        return 1;
    case NODE_CALL:
        for (i = 0; i < node->as.call.arg_count; ++i) {
            if (!compile_node(node->as.call.args[i], chunk)) return 0;
        }
        if (find_function(chunk, node->as.call.name, &index))
            return chunk_emit(chunk, OP_CALL) && chunk_emit_u32(chunk, index)
                && chunk_emit_u32(chunk, (uint32_t)node->as.call.arg_count);
        if (find_builtin(node->as.call.name, &index))
            return chunk_emit(chunk, OP_CALL_BUILTIN) && chunk_emit_u32(chunk, index)
                && chunk_emit_u32(chunk, (uint32_t)node->as.call.arg_count);
        return 0;
    case NODE_RETURN:
        return compile_node(node->as.return_stmt.value, chunk)
            && chunk_emit(chunk, OP_RETURN);
    case NODE_FOR_EACH: {
        uint32_t variable;
        size_t loop;
        size_t exit;
        size_t back;
        if (!compile_node(node->as.for_each.iterable, chunk)
            || !chunk_emit(chunk, OP_GET_ITER)
            || !chunk_intern_name(chunk, node->as.for_each.variable, &variable)) return 0;
        loop = chunk->code_count;
        if (!chunk_emit(chunk, OP_ITER_NEXT)) return 0;
        exit = chunk->code_count;
        if (!chunk_emit_u32(chunk, 0)
            || !emit_index(chunk, OP_STORE, variable)
            || !compile_node(node->as.for_each.body, chunk)) return 0;
        back = emit_jump(chunk, OP_JUMP);
        return back != SIZE_MAX && patch_jump(chunk, back, loop)
            && patch_jump(chunk, exit, chunk->code_count);
    }
    case NODE_IF: {
        size_t otherwise;
        size_t end;
        if (!compile_node(node->as.if_stmt.condition, chunk)) return 0;
        otherwise = emit_jump(chunk, OP_JUMP_IF_FALSE);
        if (otherwise == SIZE_MAX
            || !compile_node(node->as.if_stmt.then_branch, chunk)) return 0;
        if (node->as.if_stmt.else_branch != NULL) {
            end = emit_jump(chunk, OP_JUMP);
            if (end == SIZE_MAX || !patch_jump(chunk, otherwise, chunk->code_count)
                || !compile_node(node->as.if_stmt.else_branch, chunk)) return 0;
            return patch_jump(chunk, end, chunk->code_count);
        }
        return patch_jump(chunk, otherwise, chunk->code_count);
    }
    case NODE_WHILE: {
        size_t start = chunk->code_count;
        size_t exit;
        if (!compile_node(node->as.while_stmt.condition, chunk)) return 0;
        exit = emit_jump(chunk, OP_JUMP_IF_FALSE);
        if (exit == SIZE_MAX || !compile_node(node->as.while_stmt.body, chunk)) return 0;
        {
            size_t back = emit_jump(chunk, OP_JUMP);
            return back != SIZE_MAX && patch_jump(chunk, back, start)
                && patch_jump(chunk, exit, chunk->code_count);
        }
    }
    case NODE_TRY_CATCH: {
        uint32_t error_name;
        size_t handler_operand = chunk->code_count + 1;
        size_t skip_handler;
        if (!chunk_intern_name(chunk, node->as.try_catch.error_name, &error_name)
            || !chunk_emit(chunk, OP_TRY_BEGIN) || !chunk_emit_u32(chunk, 0)
            || !compile_node(node->as.try_catch.body, chunk)) return 0;
        chunk->current_line = node->line > UINT32_MAX ? UINT32_MAX : (uint32_t)node->line;
        if (!chunk_emit(chunk, OP_TRY_END)) return 0;
        skip_handler = emit_jump(chunk, OP_JUMP);
        if (skip_handler == SIZE_MAX
            || !patch_jump(chunk, handler_operand, chunk->code_count)
            || !emit_index(chunk, OP_STORE, error_name)
            || !compile_node(node->as.try_catch.handler, chunk)) return 0;
        return patch_jump(chunk, skip_handler, chunk->code_count);
    }
    case NODE_REPEAT: {
        char hidden[64];
        uint32_t hidden_index;
        uint32_t loop_index;
        size_t start;
        size_t exit;
        size_t back;
        (void)snprintf(hidden, sizeof(hidden), "__simpl_repeat_%lu",
                       (unsigned long)chunk->name_count);
        if (!compile_node(node->as.repeat_stmt.count, chunk)
            || !chunk_intern_name(chunk, hidden, &hidden_index)
            || !emit_index(chunk, OP_STORE, hidden_index)
            || !chunk_intern_name(chunk, node->as.repeat_stmt.variable, &loop_index)) return 0;
        {
            PyObject *zero = PyLong_FromLong(0);
            int ok;
            if (zero == NULL) return 0;
            ok = chunk_add_constant(chunk, zero, &index);
            Py_DECREF(zero);
            if (!ok || !emit_index(chunk, OP_CONST, index)
                || !emit_index(chunk, OP_STORE, loop_index)) return 0;
        }
        start = chunk->code_count;
        if (!emit_index(chunk, OP_LOAD, loop_index)
            || !emit_index(chunk, OP_LOAD, hidden_index)
            || !chunk_emit(chunk, OP_LT)) return 0;
        exit = emit_jump(chunk, OP_JUMP_IF_FALSE);
        if (exit == SIZE_MAX || !compile_node(node->as.repeat_stmt.body, chunk)
            || !emit_index(chunk, OP_LOAD, loop_index)) return 0;
        {
            PyObject *one = PyLong_FromLong(1);
            int ok;
            if (one == NULL) return 0;
            ok = chunk_add_constant(chunk, one, &index);
            Py_DECREF(one);
            if (!ok || !emit_index(chunk, OP_CONST, index)
                || !chunk_emit(chunk, OP_ADD)
                || !emit_index(chunk, OP_STORE, loop_index)) return 0;
        }
        back = emit_jump(chunk, OP_JUMP);
        return back != SIZE_MAX && patch_jump(chunk, back, start)
            && patch_jump(chunk, exit, chunk->code_count);
    }
    case NODE_LET:
    case NODE_ASSIGN: {
        const char *name = node->type == NODE_LET
            ? node->as.declaration.name : node->as.assign.name;
        const Node *value = node->type == NODE_LET
            ? node->as.declaration.value : node->as.assign.value;
        return compile_node(value, chunk)
            && chunk_intern_name(chunk, name, &index)
            && emit_index(chunk, OP_STORE, index);
    }
    case NODE_SAY:
        return compile_node(node->as.say.expr, chunk)
            && chunk_emit(chunk, OP_SAY);
    case NODE_LIST:
        for (i = 0; i < node->as.list.count; ++i) {
            if (!compile_node(node->as.list.items[i], chunk)) return 0;
        }
        return chunk_emit(chunk, OP_MAKE_LIST)
            && chunk_emit_u32(chunk, (uint32_t)node->as.list.count);
    case NODE_DICT:
        for (i = 0; i < node->as.dict.count; ++i) {
            if (!compile_node(node->as.dict.keys[i], chunk)
                || !compile_node(node->as.dict.values[i], chunk)) return 0;
        }
        return chunk_emit(chunk, OP_MAKE_DICT)
            && chunk_emit_u32(chunk, (uint32_t)node->as.dict.count);
    case NODE_INDEX_GET:
        return compile_node(node->as.index_get.target, chunk)
            && compile_node(node->as.index_get.index, chunk)
            && chunk_emit(chunk, OP_INDEX_GET);
    case NODE_INDEX_SET:
        return compile_node(node->as.index_set.target, chunk)
            && compile_node(node->as.index_set.index, chunk)
            && compile_node(node->as.index_set.value, chunk)
            && chunk_emit(chunk, OP_INDEX_SET);
    case NODE_NUM: {
        PyObject *value = number_value(node->as.number.value);
        int ok;
        if (value == NULL) return 0;
        ok = add_constant_cached(chunk, value, &index);
        Py_DECREF(value);
        return ok && emit_index(chunk, OP_CONST, index);
    }
    case NODE_STR: {
        PyObject *value = PyUnicode_FromString(node->as.string.value);
        int ok;
        if (value == NULL) return 0;
        ok = add_constant_cached(chunk, value, &index);
        Py_DECREF(value);
        return ok && emit_index(chunk, OP_CONST, index);
    }
    case NODE_BOOL: {
        PyObject *value = node->as.boolean.value ? Py_True : Py_False;
        Py_INCREF(value);
        if (!chunk_add_constant(chunk, value, &index)) {
            Py_DECREF(value);
            return 0;
        }
        Py_DECREF(value);
        return emit_index(chunk, OP_CONST, index);
    }
    case NODE_VAR:
        return chunk_intern_name(chunk, node->as.variable.name, &index)
            && emit_index(chunk, OP_LOAD, index);
    case NODE_BINOP:
        return compile_node(node->as.binop.left, chunk)
            && compile_node(node->as.binop.right, chunk)
            && compile_binary(node->as.binop.op, chunk);
    case NODE_UNOP:
        return compile_node(node->as.unop.value, chunk)
            && chunk_emit(chunk, node->as.unop.op == TOKEN_MINUS ? OP_NEG : OP_NOT);
    case NODE_ASK:
        return chunk_emit(chunk, OP_ASK);
    }
    return 0;
}

int compiler_compile(const Node *program, Chunk *chunk)
{
    size_t i;
    chunk_init_empty(chunk);
    if (program->type != NODE_PROGRAM) return 0;

    for (i = 0; i < program->as.program.count; ++i) {
        const Node *node = program->as.program.items[i];
        if (node->type == NODE_FUNC_DECL) {
            uint32_t name_index;
            uint32_t function_index;
            uint32_t *params = NULL;
            size_t j;
            if (!chunk_intern_name(chunk, node->as.function.name, &name_index)) goto fail;
            if (node->as.function.param_count > 0) {
                params = malloc(node->as.function.param_count * sizeof(*params));
                if (params == NULL) goto fail;
                for (j = 0; j < node->as.function.param_count; ++j) {
                    if (!chunk_intern_name(chunk, node->as.function.params[j], &params[j])) {
                        free(params);
                        goto fail;
                    }
                }
            }
            if (!chunk_add_function(chunk, name_index, params,
                                    node->as.function.param_count, &function_index)) {
                free(params);
                goto fail;
            }
            free(params);
        }
    }

    for (i = 0; i < program->as.program.count; ++i) {
        const Node *node = program->as.program.items[i];
        if (node->type != NODE_FUNC_DECL && !compile_node(node, chunk)) goto fail;
    }
    if (!chunk_emit(chunk, OP_HALT)) goto fail;
    for (i = 0; i < program->as.program.count; ++i) {
        const Node *node = program->as.program.items[i];
        uint32_t function_index;
        if (node->type != NODE_FUNC_DECL) continue;
        if (!find_function(chunk, node->as.function.name, &function_index)) goto fail;
        chunk->functions[function_index].address = chunk->code_count;
        if (!compile_node(node->as.function.body, chunk)) goto fail;
        {
            PyObject *none = Py_None;
            uint32_t constant;
            if (!chunk_add_constant(chunk, none, &constant)
                || !emit_index(chunk, OP_CONST, constant)
                || !chunk_emit(chunk, OP_RETURN)) goto fail;
        }
    }
    return 1;

fail:
        chunk_free(chunk);
        return 0;
}
