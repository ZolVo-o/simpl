#include "chunk.h"

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
