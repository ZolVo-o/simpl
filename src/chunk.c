#include "chunk.h"

#include "opcodes.h"

#include <stdlib.h>
#include <string.h>

void chunk_init_empty(Chunk *chunk)
{
    memset(chunk, 0, sizeof(*chunk));
}

static int reserve_code(Chunk *chunk, size_t needed)
{
    size_t capacity = chunk->code_capacity == 0 ? 16 : chunk->code_capacity;
    uint8_t *code;
    uint32_t *lines;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*lines)) return 0;
    if (capacity == chunk->code_capacity) return 1;
    lines = realloc(chunk->lines, capacity * sizeof(*lines));
    if (lines == NULL) return 0;
    chunk->lines = lines;
    code = realloc(chunk->code, capacity);
    if (code == NULL) return 0;
    chunk->code = code;
    chunk->code_capacity = capacity;
    return 1;
}

int chunk_emit(Chunk *chunk, uint8_t byte)
{
    if (chunk->code_count == SIZE_MAX
        || !reserve_code(chunk, chunk->code_count + 1)) return 0;
    chunk->code[chunk->code_count++] = byte;
    chunk->lines[chunk->code_count - 1] = chunk->current_line;
    return 1;
}

int chunk_emit_u32(Chunk *chunk, uint32_t value)
{
    return chunk_emit(chunk, (uint8_t)value)
        && chunk_emit(chunk, (uint8_t)(value >> 8))
        && chunk_emit(chunk, (uint8_t)(value >> 16))
        && chunk_emit(chunk, (uint8_t)(value >> 24));
}

int chunk_add_constant(Chunk *chunk, PyObject *value, uint32_t *index)
{
    PyObject **constants;
    if (chunk->constant_count >= UINT32_MAX
        || chunk->constant_count == SIZE_MAX / sizeof(*constants)) return 0;
    constants = realloc(chunk->constants,
                        (chunk->constant_count + 1) * sizeof(*constants));
    if (constants == NULL) return 0;
    chunk->constants = constants;
    *index = (uint32_t)chunk->constant_count;
    Py_INCREF(value);
    chunk->constants[chunk->constant_count++] = value;
    return 1;
}

int chunk_intern_name(Chunk *chunk, const char *name, uint32_t *index)
{
    size_t i;
    char **names;
    size_t length = strlen(name);
    for (i = 0; i < chunk->name_count; ++i) {
        if (strcmp(chunk->names[i], name) == 0) {
            *index = (uint32_t)i;
            return 1;
        }
    }
    if (chunk->name_count >= UINT32_MAX
        || chunk->name_count == SIZE_MAX / sizeof(*names)) return 0;
    names = realloc(chunk->names, (chunk->name_count + 1) * sizeof(*names));
    if (names == NULL) return 0;
    chunk->names = names;
    names[chunk->name_count] = malloc(length + 1);
    if (names[chunk->name_count] == NULL) return 0;
    memcpy(names[chunk->name_count], name, length + 1);
    *index = (uint32_t)chunk->name_count++;
    return 1;
}

int chunk_add_function(Chunk *chunk, uint32_t name_index,
                       const uint32_t *parameters, size_t parameter_count,
                       uint32_t *function_index)
{
    ChunkFunction *functions;
    uint32_t *parameter_copy = NULL;
    size_t i;
    if (chunk->function_count >= UINT32_MAX
        || chunk->function_count == SIZE_MAX / sizeof(*functions)
        || parameter_count > SIZE_MAX / sizeof(*parameter_copy)) return 0;
    if (parameter_count > 0) {
        parameter_copy = malloc(parameter_count * sizeof(*parameter_copy));
        if (parameter_copy == NULL) return 0;
        for (i = 0; i < parameter_count; ++i) parameter_copy[i] = parameters[i];
    }
    functions = realloc(chunk->functions,
                        (chunk->function_count + 1) * sizeof(*functions));
    if (functions == NULL) {
        free(parameter_copy);
        return 0;
    }
    chunk->functions = functions;
    *function_index = (uint32_t)chunk->function_count;
    functions[chunk->function_count].name_index = name_index;
    functions[chunk->function_count].parameters = parameter_copy;
    functions[chunk->function_count].parameter_count = parameter_count;
    functions[chunk->function_count].address = 0;
    ++chunk->function_count;
    return 1;
}

int chunk_init(Chunk *chunk, PyObject *const *constants, size_t constant_count,
               const uint8_t *code, size_t code_count)
{
    size_t i;
    chunk_init_empty(chunk);
    for (i = 0; i < constant_count; ++i) {
        uint32_t index;
        if (!chunk_add_constant(chunk, constants[i], &index)) {
            chunk_free(chunk);
            return 0;
        }
    }
    for (i = 0; i < code_count; ++i) {
        if (!chunk_emit(chunk, code[i])) {
            chunk_free(chunk);
            return 0;
        }
    }
    return 1;
}

static int read_u32(const Chunk *chunk, size_t *offset, uint32_t *value)
{
    if (chunk->code_count - *offset < 4) return 0;
    *value = (uint32_t)chunk->code[*offset]
        | ((uint32_t)chunk->code[*offset + 1] << 8)
        | ((uint32_t)chunk->code[*offset + 2] << 16)
        | ((uint32_t)chunk->code[*offset + 3] << 24);
    *offset += 4;
    return 1;
}

static int jump_target(const Chunk *chunk, size_t offset, uint32_t raw,
                       size_t *target)
{
    int32_t delta = (int32_t)raw;
    if ((delta < 0 && (size_t)(-(int64_t)delta) > offset)
        || (delta > 0 && (size_t)delta > chunk->code_count - offset)) return 0;
    *target = (size_t)((int64_t)offset + delta);
    return *target < chunk->code_count;
}

int chunk_verify(const Chunk *chunk)
{
    unsigned char *starts;
    size_t offset = 0;
    size_t i;
    int has_halt = 0;

    if (chunk == NULL || chunk->code_count == 0 || chunk->code == NULL || chunk->lines == NULL
        || (chunk->constant_count != 0 && chunk->constants == NULL)
        || (chunk->name_count != 0 && chunk->names == NULL)
        || (chunk->function_count != 0 && chunk->functions == NULL)) return 0;
    for (i = 0; i < chunk->constant_count; ++i)
        if (chunk->constants[i] == NULL) return 0;
    for (i = 0; i < chunk->name_count; ++i)
        if (chunk->names[i] == NULL) return 0;
    starts = calloc(chunk->code_count, sizeof(*starts));
    if (starts == NULL) return 0;
    while (offset < chunk->code_count) {
        uint8_t opcode;
        uint32_t first, second;
        starts[offset] = 1;
        opcode = chunk->code[offset++];
        if (opcode > OP_TRY_END) goto fail;
        switch (opcode) {
        case OP_CONST:
            if (!read_u32(chunk, &offset, &first) || first >= chunk->constant_count) goto fail;
            break;
        case OP_LOAD:
        case OP_STORE:
            if (!read_u32(chunk, &offset, &first) || first >= chunk->name_count) goto fail;
            break;
        case OP_CALL:
            if (!read_u32(chunk, &offset, &first) || !read_u32(chunk, &offset, &second)
                || first >= chunk->function_count
                || second != chunk->functions[first].parameter_count) goto fail;
            break;
        case OP_CALL_BUILTIN:
            if (!read_u32(chunk, &offset, &first) || !read_u32(chunk, &offset, &second)
                || first > BUILTIN_EXIT) goto fail;
            break;
        case OP_MAKE_LIST:
        case OP_MAKE_DICT:
            if (!read_u32(chunk, &offset, &first)) goto fail;
            break;
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_ITER_NEXT:
        case OP_TRY_BEGIN:
            if (!read_u32(chunk, &offset, &first)) goto fail;
            break;
        case OP_HALT:
            has_halt = 1;
            break;
        default:
            break;
        }
    }
    if (!has_halt) goto fail;
    for (i = 0; i < chunk->function_count; ++i) {
        if (chunk->functions[i].name_index >= chunk->name_count
            || chunk->functions[i].address >= chunk->code_count
            || !starts[chunk->functions[i].address]
            || (chunk->functions[i].parameter_count != 0
                && chunk->functions[i].parameters == NULL)) goto fail;
        {
            size_t parameter;
            for (parameter = 0; parameter < chunk->functions[i].parameter_count; ++parameter)
                if (chunk->functions[i].parameters[parameter] >= chunk->name_count) goto fail;
        }
    }
    offset = 0;
    while (offset < chunk->code_count) {
        uint8_t opcode = chunk->code[offset++];
        uint32_t operand;
        size_t target;
        switch (opcode) {
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_ITER_NEXT:
        case OP_TRY_BEGIN:
            if (!read_u32(chunk, &offset, &operand)
                || !jump_target(chunk, offset, operand, &target) || !starts[target]) goto fail;
            break;
        case OP_CONST:
        case OP_LOAD:
        case OP_STORE:
        case OP_MAKE_LIST:
        case OP_MAKE_DICT:
            offset += 4;
            break;
        case OP_CALL:
        case OP_CALL_BUILTIN:
            offset += 8;
            break;
        default:
            break;
        }
    }
    free(starts);
    return 1;

fail:
    free(starts);
    return 0;
}

void chunk_free(Chunk *chunk)
{
    size_t i;
    for (i = 0; i < chunk->constant_count; ++i) Py_DECREF(chunk->constants[i]);
    for (i = 0; i < chunk->name_count; ++i) free(chunk->names[i]);
    for (i = 0; i < chunk->function_count; ++i) {
        free(chunk->functions[i].parameters);
    }
    free(chunk->constants);
    free(chunk->names);
    free(chunk->functions);
    free(chunk->code);
    free(chunk->lines);
    chunk_init_empty(chunk);
}
