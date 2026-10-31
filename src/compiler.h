#ifndef SIMPL_COMPILER_H
#define SIMPL_COMPILER_H

#include "ast.h"
#include "chunk.h"

int compiler_compile(const Node *program, Chunk *chunk);
int compiler_compile_with_error_line(const Node *program, Chunk *chunk,
                                    uint32_t *error_line);

#endif
