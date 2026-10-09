#ifndef SIMPL_COMPILER_H
#define SIMPL_COMPILER_H

#include "ast.h"
#include "chunk.h"

int compiler_compile(const Node *program, Chunk *chunk);

#endif
