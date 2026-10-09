#ifndef SIMPL_VM_H
#define SIMPL_VM_H

#include "chunk.h"

int vm_run(const Chunk *chunk);
int vm_run_with_source(const Chunk *chunk, const char *source_path);

#endif
