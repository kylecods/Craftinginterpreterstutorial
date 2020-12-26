#ifndef croto_debug_h
#define croto_debug_h

#include "chunk.h"


void disassemble_chunk(Chunk *chunk, const char* name);
int disassemble_instr(Chunk *chunk, int offset);

#endif
