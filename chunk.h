#ifndef file_chunk_h
#define file_chunk_h

#include "common.h"
#include "value.h"

typedef enum{//note: implement different op instr for comparison operators
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_SUPER,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_NEGATE,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_BITWISE_AND,
  OP_BITWISE_OR,
  OP_BITWISE_XOR,
  OP_RIGHT_SHIFT,
  OP_LEFT_SHIFT,
  OP_NOT,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_BUILD_LIST,
  OP_WIDE,
  OP_INDEX_SUBSCR,
  OP_STORE_SUBSCR,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD
}Opcode;

typedef struct{
  int count;
  int capacity;
  uint8_t *code;
  int *lines;
  ValueArray constants;
}Chunk;

void init_chunk(Chunk *chunk);
void free_chunk(RotoVM* vm,Chunk *chunk);
void write_chunk(RotoVM* vm,Chunk *chunk, uint8_t byte, int line);

int add_constant(RotoVM* vm,Chunk *chunk, Value value);
#endif
