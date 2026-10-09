#ifndef SIMPL_CHUNK_H
#define SIMPL_CHUNK_H

#include <Python.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t name_index;
    uint32_t *parameters;
    size_t parameter_count;
    size_t address;
} ChunkFunction;

typedef struct {
    uint8_t *code;
    uint32_t *lines;
    size_t code_count;
    size_t code_capacity;
    uint32_t current_line;
    PyObject **constants;
    size_t constant_count;
    char **names;
    size_t name_count;
    ChunkFunction *functions;
    size_t function_count;
} Chunk;

void chunk_init_empty(Chunk *chunk);
int chunk_init(Chunk *chunk, PyObject *const *constants, size_t constant_count,
               const uint8_t *code, size_t code_count);
int chunk_add_constant(Chunk *chunk, PyObject *value, uint32_t *index);
int chunk_intern_name(Chunk *chunk, const char *name, uint32_t *index);
int chunk_add_function(Chunk *chunk, uint32_t name_index,
                       const uint32_t *parameters, size_t parameter_count,
                       uint32_t *function_index);
int chunk_emit(Chunk *chunk, uint8_t byte);
int chunk_emit_u32(Chunk *chunk, uint32_t value);
void chunk_free(Chunk *chunk);

#endif
