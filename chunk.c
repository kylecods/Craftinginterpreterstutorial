#include <stdlib.h>

#include "chunk.h"
#include "memory.h"



void init_chunk(Chunk *chunk) {
  /* code */
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  init_val_array(&chunk->constants);
}

void free_chunk(Chunk *chunk) {
  /* code */
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  free_val_array(&chunk->constants);
  init_chunk(chunk);

}
/* **THINGS TO REMEMBER** for bytcode space allocation:
*1.Allocate a new array with more capacity.
*2.Copy the existing elements from the old array to the new one.
*3.Store the new capacity.
*4.Delete the old array.
*5.Update code to point to the new array.
*6.Store the element in the new array now that there is room.
*7.Update the count.
*/
void write_chunk(Chunk *chunk, uint8_t byte, int line){
  if(chunk->capacity < chunk->count + 1){
    int old_cap = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_cap);
    chunk->code = GROW_ARRAY(chunk->code, uint8_t, old_cap, chunk->capacity);
    chunk->lines = GROW_ARRAY(chunk->lines, int, old_cap, chunk->capacity);
  }
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}
int add_constant(Chunk *chunk, Value value){
  write_val_array(&chunk->constants, value);
  return chunk->constants.count - 1;
}
