#ifndef SIMPL_SERIALIZE_H
#define SIMPL_SERIALIZE_H

#include "chunk.h"

int serialize_save(const Chunk *chunk, const char *path);
/* chunk must have been initialized with chunk_init_empty before loading. */
int serialize_load(Chunk *chunk, const char *path);

#endif
