#ifndef SIMPL_DISASM_H
#define SIMPL_DISASM_H

#include "chunk.h"

#include <stdio.h>

int disasm_dump(const Chunk *chunk, FILE *output);
int disasm_dump_options(const Chunk *chunk, FILE *output,
                        int no_names, int stats);

#endif
