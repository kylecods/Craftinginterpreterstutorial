#ifndef croto_vm_h
#define croto_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"
#include "object.h"
#include "lox.h"


#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

//single ongoing function call
typedef struct{
    ObjClosure* closure;
    uint8_t* ip;
    Value* slots;//points to VMs value stack at the first slot the function can use
} CallFrame;

struct LoxVM{
  CallFrame frames[FRAMES_MAX];
  int frameCount;//height of the callframe stack
  Value stack[STACK_MAX];//stack
  Value *stack_top; //the top or beginning of the stack
  Table strings; //for string interning
  Table globals; //for globals
  ObjUpvalue* open_upvalues;

  size_t bytes_alocated;//running total of no of bytes of managed memory
  size_t next_gc;//threshold that triggers next collection

  Obj* objects;
  int gray_count;
  int gray_capacity;
  Obj** gray_stack;
};

//typedef enum{
  //INTERPRET_OK,
  //INTERPRET_COMPILE_ERROR,
  //INTERPRET_RUNTIME_ERROR
//}InterpretResult;

//extern VM vm;

//void init_vm();
//void free_vm();
void push(LoxVM* vm, Value value);
Value pop(LoxVM* vm);

void define_native(LoxVM* vm, const char* name, NativeFn function);
void runtime_error(LoxVM* vm, const char* format, ...);
//InterpretResult interpret(const char* source);
#endif
