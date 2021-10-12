#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"


void init_chunk(Chunk *chunk) {
  /* code */
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  init_val_array(&chunk->constants);
}

void free_chunk(RotoVM* vm,Chunk *chunk) {
  /* code */
  FREE_ARRAY(vm,uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(vm,int, chunk->lines, chunk->capacity);
  free_val_array(vm,&chunk->constants);
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
void write_chunk(RotoVM* vm,Chunk *chunk, uint8_t byte, int line){
  if(chunk->capacity < chunk->count + 1){
    int old_cap = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(old_cap);
    chunk->code = GROW_ARRAY(vm,chunk->code, uint8_t, old_cap, chunk->capacity);
    chunk->lines = GROW_ARRAY(vm,chunk->lines, int, old_cap, chunk->capacity);
  }
  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}
int add_constant(RotoVM* vm,Chunk *chunk, Value value){
    //make new constants reachable for the GC
    push(vm,value);
    //GC trigger
    write_val_array(vm,&chunk->constants, value);
    pop(vm);
    return chunk->constants.count - 1;
}
