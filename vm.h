#ifndef croto_vm_h
#define croto_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

//single ongoing function call
typedef struct{
    ObjFunction* function;
    uint8_t* ip;
    Value* slots;//points to VMs value stack at the first slot the function can use
} CallFrame;

typedef struct{
  CallFrame frames[FRAMES_MAX];
  int frameCount;//height of the callframe stack
  Value stack[STACK_MAX];//stack
  Value *stack_top; //the top or beginning of the stack
  Table strings; //for string interning
  Table globals; //for globals

  Obj* objects;
}VM;

typedef enum{
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
}InterpretResult;

extern VM vm;

void init_vm();
void free_vm();
void push(Value value);
Value pop();
InterpretResult interpret(const char* source);
#endif
